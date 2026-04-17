module;

#include <cassert>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>


export module mo_yanxi.backend.vulkan.attachment_manager;

import std;
import mo_yanxi.gui.renderer.frontend; // for blending_type
import mo_yanxi.math.vector2;
import mo_yanxi.vk.util;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk;

namespace mo_yanxi::backend::vulkan{
using namespace gui;

export
enum struct mask_usage{
	ignore,
	read,
	write,
};

export struct attachment_config{
	VkFormat format;
	VkImageUsageFlags usage;
};

export struct draw_attachment_config{
	attachment_config attachment{};
};

export struct draw_attachment_create_info{
	std::vector<draw_attachment_config> attachments;
	// Attachment 0 usually implies depth/stencil or main color, depends on usage
	VkSampleCountFlagBits multisample{VK_SAMPLE_COUNT_1_BIT};

	bool enables_multisample() const noexcept{
		return multisample != VK_SAMPLE_COUNT_1_BIT;
	}
};

export struct blit_attachment_create_info{
	std::vector<attachment_config> attachments;
};

// --- 附件管理器类 ---

export class attachment_manager{
private:
	vk::allocator_usage allocator_;
	draw_attachment_create_info draw_config_;
	blit_attachment_create_info blit_config_;

	/**
	 * @brief {{draw_attachments...}, {blit_attachments...}, {multi_sample_attachments...}}
	 */
	std::vector<vk::combined_image> attachments_{};


	std::vector<vk::image_view> mask_image_views_{};
	vk::combined_image mask_image_{};

public:
	[[nodiscard]] attachment_manager() = default;

	[[nodiscard]] attachment_manager(
		vk::allocator_usage allocator,
		draw_attachment_create_info&& draw_info,
		blit_attachment_create_info&& blit_info
	) : allocator_(allocator)
	    , draw_config_(std::move(draw_info))
	    , blit_config_(std::move(blit_info)){
		// 预分配空间
		const auto total_count = draw_config_.attachments.size() * (1 + draw_config_.enables_multisample()) +
			blit_config_.attachments.size();
		attachments_.resize(total_count);
	}

private:
	void make_mask_(){
		auto [w, h] = get_extent();

		mask_image_ = vk::combined_image{
				vk::image{
					allocator_, {
						.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
						.imageType = VK_IMAGE_TYPE_2D,
						.format = VK_FORMAT_R8_UNORM,
						.extent = {w, h, 1},
						.mipLevels = 1,
						.arrayLayers = static_cast<std::uint32_t>(mask_image_views_.size()),
						.samples = VK_SAMPLE_COUNT_1_BIT,
						.tiling = VK_IMAGE_TILING_OPTIMAL,
						.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
					},
					VmaAllocationCreateInfo{
						.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
					}
				},
				VkImageViewCreateInfo{
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
					.format = VK_FORMAT_R8_UNORM,
					.components = {},
					.subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = static_cast<std::uint32_t>(mask_image_views_.size())
					}
				}
			};

		const auto d = allocator_.get_device();
		for(auto&& [idx, mask_image_view] : mask_image_views_ | std::views::enumerate){

			VkComponentMapping mapping{};
			if (idx == 0) {
				mapping.r = VK_COMPONENT_SWIZZLE_ONE;
				mapping.g = VK_COMPONENT_SWIZZLE_ZERO;
				mapping.b = VK_COMPONENT_SWIZZLE_ZERO;
				mapping.a = VK_COMPONENT_SWIZZLE_ZERO;
			}

			mask_image_view = vk::image_view{
				d, {
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.image = mask_image_.get_image(),
					.viewType = VK_IMAGE_VIEW_TYPE_2D,
					.format = VK_FORMAT_R8_UNORM,
					.components = mapping,
					.subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = static_cast<std::uint32_t>(idx),
						.layerCount = 1
					}
				}
			};
		}
	}

public:
	bool update_mask_depth(unsigned depth){
		if(depth <= mask_image_views_.size()) return false;
		if(depth == 0) return false;
		mask_image_views_.resize(depth);

		make_mask_();

		return true;
	}

	/**
	 * @brief 执行 Resize 操作，重新创建图像资源并转换布局
	 * @param extent 新的尺寸
	 * @param cmd 用于执行布局转换 Barrier 的 Command Buffer (通常是 Transient Command Buffer)
	 * @return 如果尺寸未改变返回 false，执行了重建返回 true
	 */
	bool resize(VkExtent2D extent){
		// 检查尺寸是否需要变更
		if(!attachments_.empty() && attachments_[0].get_image().get()){
			const auto [ox, oy] = attachments_.front().get_image().get_extent2();
			if(extent.width == ox && extent.height == oy) return false;
		}

		// 定义 View 创建辅助 lambda
		static constexpr auto get_view_create_info = [](VkFormat format){
			return VkImageViewCreateInfo{
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.viewType = VK_IMAGE_VIEW_TYPE_2D,
					.format = format,
					.subresourceRange = vk::image::default_image_subrange
				};
		};

		const auto draw_count = get_draw_attachment_count();
		const auto blit_count = get_blit_attachment_count();

		// 1. 重建 Draw Attachments
		for(const auto& [idx, cfg] : draw_config_.attachments | std::views::enumerate){
			attachments_[idx] = vk::combined_image{
					vk::image{
						allocator_,
						{extent.width, extent.height, 1},

						VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | cfg.attachment.usage,
						cfg.attachment.format
					},
					get_view_create_info(cfg.attachment.format)
				};
		}

		// 2. 重建 Blit Attachments
		for(const auto& [idx, cfg] : blit_config_.attachments | std::views::enumerate){
			const auto global_idx = idx + draw_count;
			attachments_[global_idx] = vk::combined_image{
					vk::image{
						allocator_,
						{extent.width, extent.height, 1},
						VK_IMAGE_USAGE_STORAGE_BIT | cfg.usage,
						cfg.format
					},
					get_view_create_info(cfg.format)
				};
		}

		// 3. 重建 MSAA Attachments (如果启用)
		if(draw_config_.enables_multisample()){
			for(const auto& [idx, cfg] : draw_config_.attachments | std::views::enumerate){
				attachments_[idx + draw_count + blit_count] = vk::combined_image{
						vk::image{
							allocator_,
							{extent.width, extent.height, 1},
							VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
							cfg.attachment.format,
							1, 1, draw_config_.multisample
						},
						get_view_create_info(cfg.attachment.format)
					};
			}
		}

		if(mask_image_views_.size()){
			make_mask_();
		}

		return true;
	}

	// --- Getters ---

	[[nodiscard]] std::uint32_t get_draw_attachment_count() const noexcept{
		return static_cast<std::uint32_t>(draw_config_.attachments.size());
	}

	[[nodiscard]] std::uint32_t get_blit_attachment_count() const noexcept{
		return static_cast<std::uint32_t>(blit_config_.attachments.size());
	}

	[[nodiscard]] const draw_attachment_create_info& get_draw_config() const noexcept{
		return draw_config_;
	}

	[[nodiscard]] const blit_attachment_create_info& get_blit_config() const noexcept{
		return blit_config_;
	}

	[[nodiscard]] const vk::combined_image& get_mask_image() const noexcept{
		return mask_image_;
	}

	[[nodiscard]] std::span<const vk::image_view> get_mask_image_views() const noexcept{
		return mask_image_views_;
	}

	// 获取用于绘制的附件 (非 MSAA 的 Resolve 目标，或单采样的直接渲染目标)
	[[nodiscard]] std::span<const vk::combined_image> get_draw_attachments() const noexcept{
		return {attachments_.data(), get_draw_attachment_count()};
	}

	// 获取用于 Blit/Compute 的附件
	[[nodiscard]] std::span<const vk::combined_image> get_blit_attachments() const noexcept{
		return {attachments_.data() + get_draw_attachment_count(), get_blit_attachment_count()};
	}

	// 获取 MSAA 附件
	[[nodiscard]] std::span<const vk::combined_image> get_multisample_attachments() const noexcept{
		return {attachments_.begin() + get_draw_attachment_count() + get_blit_attachment_count(), attachments_.end()};
	}

	[[nodiscard]] bool enables_multisample() const noexcept{
		return draw_config_.enables_multisample();
	}

	[[nodiscard]] VkExtent2D get_extent() const noexcept{
		if(attachments_.empty()) return {0, 0};
		const auto [w, h] = attachments_.front().get_image().get_extent2();
		return {w, h};
	}

	[[nodiscard]] VkRect2D get_screen_area() const noexcept{
		return {{}, get_extent()};
	}

	// 获取主附件（通常作为后续处理的 Base）
	[[nodiscard]] vk::image_handle get_base_image() const noexcept{
		auto blit_atts = get_blit_attachments();
		if(!blit_atts.empty()) return blit_atts.front();
		return attachments_.front(); // Fallback
	}

	// 辅助生成 Dynamic Rendering 信息
	template <std::size_t N>
	void configure_dynamic_rendering(
		vk::dynamic_rendering& target,
		std::bitset<N> use_mask,
		std::bitset<N> input_mask, // 新增：指示哪些绝对槽位被用作 Input Attachment
		bool use_multisample_target,
		mask_usage m_usage, // 新增：识别是否正在操作 Mask
		std::uint32_t m_depth,
		VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE) const{
		target.clear_color_attachments();
		const bool enables_multisample_ = use_multisample_target && enables_multisample();

		if (m_usage == mask_usage::write) {
			target.push_color_attachment(
				mask_image_views_[m_depth].get(),
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				loadOp, // 写入 Mask 通常需要清除旧数据
				storeOp
			);
			return;
		}

		if(use_mask.any() && !use_mask.all()){
			if(use_mask.size() < attachments_.size()){
				throw std::out_of_range("mask cannot cover attachments");
			}

			if(enables_multisample_){
				for(const auto& [idx, attac, multi] : std::views::zip(std::views::iota(0), get_draw_attachments(),
				                                                      get_multisample_attachments())){
					if(!use_mask[idx]) continue;

					bool isInput = input_mask[idx];
					// 根据 input_mask 决定布局
					VkImageLayout current_layout = isInput
						                               ? VK_IMAGE_LAYOUT_GENERAL
						                               : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

					target.push_color_attachment(
						multi.get_image_view(), current_layout,
						loadOp, !isInput ? storeOp : VK_ATTACHMENT_STORE_OP_DONT_CARE,
						attac.get_image_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // Resolve 目标保持 Optimal 即可
					);
				}
			} else{
				for(const auto& [idx, attac] : get_draw_attachments() | std::views::enumerate){
					if(!use_mask[idx]) continue;
					bool isInput = input_mask[idx];
					VkImageLayout current_layout = isInput
						                               ? VK_IMAGE_LAYOUT_GENERAL
						                               : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

					target.push_color_attachment(
						attac.get_image_view(), current_layout,
						loadOp, !isInput ? storeOp : VK_ATTACHMENT_STORE_OP_DONT_CARE);
				}
			}
		} else{
			// 原 else 分支缺少索引获取机制，这里补充 std::views::iota 和 enumerate 以便查询 input_mask
			if(enables_multisample_){
				for(const auto& [idx, attac, multi] : std::views::zip(std::views::iota(0u), get_draw_attachments(), get_multisample_attachments())){
					VkImageLayout current_layout = input_mask[idx]
						                               ? VK_IMAGE_LAYOUT_GENERAL
						                               : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

					target.push_color_attachment(
						multi.get_image_view(), current_layout,
						loadOp, storeOp,
						attac.get_image_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
					);
				}
			} else{
				for(const auto& [idx, attac] : get_draw_attachments() | std::views::enumerate){
					VkImageLayout current_layout = input_mask[idx]
						                               ? VK_IMAGE_LAYOUT_GENERAL
						                               : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

					target.push_color_attachment(
						attac.get_image_view(), current_layout,
						loadOp, storeOp);
				}
			}
		}
	}
};
}
