module;

#include <cassert>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "gch/small_vector.hpp"

export module mo_yanxi.backend.vulkan.attachment_manager;

import mo_yanxi.gui.renderer.frontend; // for blending_type
import mo_yanxi.math.vector2;
import mo_yanxi.vk.util;
import mo_yanxi.vk.cmd;
import mo_yanxi.vk;
import std;

namespace mo_yanxi::backend::vulkan{
using namespace gui;

// --- 配置结构体 (从原文件迁移) ---

export struct attachment_config{
	VkFormat format;
	VkImageUsageFlags usage;
};

export struct draw_attachment_config{
	attachment_config attachment{};
	std::array<blending_type, std::to_underlying(blending_type::SIZE)> swizzle{
			[]{
				std::array<blending_type, std::to_underlying(blending_type::SIZE)> arr;
				arr.fill(blending_type::SIZE);
				return arr;
			}()
		};
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

	// 存储所有附件（Draw 在前，Blit 在后，与原逻辑保持一致以便索引）
	std::vector<vk::combined_image> attachments_{};
	// 存储 MSAA 多重采样附件（如果启用）
	std::vector<vk::combined_image> attachments_multisamples_{};

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
		const auto total_count = draw_config_.attachments.size() + blit_config_.attachments.size();
		attachments_.resize(total_count);

		if(draw_config_.enables_multisample()){
			attachments_multisamples_.resize(draw_config_.attachments.size());
		}
	}

	// 禁止拷贝，允许移动
	attachment_manager(const attachment_manager&) = delete;
	attachment_manager& operator=(const attachment_manager&) = delete;
	attachment_manager(attachment_manager&&) noexcept = default;
	attachment_manager& operator=(attachment_manager&&) noexcept = default;

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

		const auto draw_count = draw_config_.attachments.size();
		const auto blit_count = blit_config_.attachments.size();

		// 1. 重建 Draw Attachments
		for(const auto& [idx, cfg] : draw_config_.attachments | std::views::enumerate){
			attachments_[idx] = vk::combined_image{
					vk::image{
						allocator_,
						{extent.width, extent.height, 1},
						VK_IMAGE_USAGE_STORAGE_BIT |
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
				attachments_multisamples_[idx] = vk::combined_image{
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

		return true;
	}

	// --- Getters ---

	[[nodiscard]] const draw_attachment_create_info& get_draw_config() const noexcept{
		return draw_config_;
	}

	[[nodiscard]] const blit_attachment_create_info& get_blit_config() const noexcept{
		return blit_config_;
	}

	// 获取用于绘制的附件 (非 MSAA 的 Resolve 目标，或单采样的直接渲染目标)
	[[nodiscard]] std::span<const vk::combined_image> get_draw_attachments() const noexcept{
		return {attachments_.data(), draw_config_.attachments.size()};
	}

	// 获取用于 Blit/Compute 的附件
	[[nodiscard]] std::span<const vk::combined_image> get_blit_attachments() const noexcept{
		return {attachments_.data() + draw_config_.attachments.size(), blit_config_.attachments.size()};
	}

	// 获取 MSAA 附件
	[[nodiscard]] std::span<const vk::combined_image> get_multisample_attachments() const noexcept{
		return attachments_multisamples_;
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
		bool use_multisample_target,
		VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE) const{
		target.clear_color_attachments();
		const bool enables_multisample_ = use_multisample_target && enables_multisample();
		if(use_mask.any() && !use_mask.all()){
			if(use_mask.size() < attachments_.size()){
				throw std::out_of_range("mask cannot cover attachments");
			}

			if(enables_multisample_){
				for(const auto& [idx, attac, multi] : std::views::zip(std::views::iota(0uz), get_draw_attachments(),
					    attachments_multisamples_)){
					if(!use_mask[idx]) continue;

					target.push_color_attachment(
						multi.get_image_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						loadOp, storeOp,
						attac.get_image_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
					);
				}
			} else{
				for(const auto& [idx, attac] : get_draw_attachments() | std::views::enumerate){
					if(!use_mask[idx]) continue;

					target.push_color_attachment(
						attac.get_image_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						loadOp, storeOp);
				}
			}
		} else{
			if(enables_multisample_){
				for(const auto& [attac, multi] : std::views::zip(get_draw_attachments(), attachments_multisamples_)){
					target.push_color_attachment(
						multi.get_image_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						loadOp, storeOp,
						attac.get_image_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
					);
				}
			} else{
				for(const auto& attac : get_draw_attachments()){
					target.push_color_attachment(
						attac.get_image_view(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						loadOp, storeOp);
				}
			}
		}
	}
};
}
