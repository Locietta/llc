#include <llc/image.h>

#include <cstddef>
#include <stdexcept>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

llc::u8 channel(const llc::Image &image, llc::u32 x, llc::u32 y, llc::usize component) {
    return std::to_integer<llc::u8>(image.row_data(y)[static_cast<llc::usize>(x) * 4 + component]);
}

void test_srgb_to_linear_rgba8() {
    llc::Image source(1, 2, rhi::Format::RGBA8UnormSrgb, 8);
    source.row_data(0)[0] = static_cast<llc::byte>(128);
    source.row_data(0)[1] = static_cast<llc::byte>(0);
    source.row_data(0)[2] = static_cast<llc::byte>(255);
    source.row_data(0)[3] = static_cast<llc::byte>(17);
    source.row_data(1)[0] = static_cast<llc::byte>(255);
    source.row_data(1)[1] = static_cast<llc::byte>(128);
    source.row_data(1)[2] = static_cast<llc::byte>(0);
    source.row_data(1)[3] = static_cast<llc::byte>(231);

    auto converted = llc::convert_image(source, rhi::Format::RGBA8Unorm);

    require(converted.format == rhi::Format::RGBA8Unorm, "sRGB conversion returned the wrong format");
    require(channel(converted, 0, 0, 0) == 55, "sRGB red channel was not converted to linear");
    require(channel(converted, 0, 0, 1) == 0, "sRGB zero channel changed during conversion");
    require(channel(converted, 0, 0, 2) == 255, "sRGB one channel changed during conversion");
    require(channel(converted, 0, 0, 3) == 17, "alpha was gamma converted");
    require(channel(converted, 0, 1, 0) == 255, "second row was not converted correctly");
    require(channel(converted, 0, 1, 1) == 55, "padded source row was not addressed correctly");
    require(channel(converted, 0, 1, 3) == 231, "second-row alpha changed during conversion");
}

void test_linear_to_srgb_rgba8() {
    llc::Image source(1, 1, rhi::Format::RGBA8Unorm, 4);
    source.row_data(0)[0] = static_cast<llc::byte>(55);
    source.row_data(0)[1] = static_cast<llc::byte>(0);
    source.row_data(0)[2] = static_cast<llc::byte>(255);
    source.row_data(0)[3] = static_cast<llc::byte>(93);

    auto converted = llc::convert_image(source, rhi::Format::RGBA8UnormSrgb);

    require(converted.format == rhi::Format::RGBA8UnormSrgb, "linear conversion returned the wrong format");
    require(channel(converted, 0, 0, 0) == 128, "linear red channel was not converted to sRGB");
    require(channel(converted, 0, 0, 1) == 0, "linear zero channel changed during conversion");
    require(channel(converted, 0, 0, 2) == 255, "linear one channel changed during conversion");
    require(channel(converted, 0, 0, 3) == 93, "alpha was gamma converted");
}

} // namespace

int main() {
    test_srgb_to_linear_rgba8();
    test_linear_to_srgb_rgba8();
    return 0;
}
