export module mo_yanxi.backend.miniaudio.audio;

import std;
export import mo_yanxi.audio;

namespace mo_yanxi::backend::miniaudio{

export [[nodiscard]] std::unique_ptr<audio::audio_driver_backend> make_audio_driver(
	audio::device_config config = {});

}
