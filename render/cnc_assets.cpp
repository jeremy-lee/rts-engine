// RTS Engine — render/cnc_assets.cpp
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "cnc_assets.h"

namespace rts {

bool CncAtlas::Load(const std::string& path, std::vector<uint8_t>& outPixels, int& outW, int& outH) {
	int channels = 0;
	stbi_uc* data = stbi_load(path.c_str(), &outW, &outH, &channels, 4);
	if (data == nullptr) return false;
	const size_t n = static_cast<size_t>(outW) * outH * 4;
	outPixels.assign(data, data + n);
	stbi_image_free(data);
	return true;
}

} // namespace rts
