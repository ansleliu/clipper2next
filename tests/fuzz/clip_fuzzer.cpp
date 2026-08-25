#include "../support/random_path_generator.h"

#include "clipper2next/batch.h"
#include "clipper2next/clipper.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <vector>

namespace next = clipper2next;
namespace support = clipper2next::tests::support;

namespace {

[[nodiscard]] auto read_u32(std::span<const std::uint8_t> data, std::size_t offset)
    -> std::uint32_t {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        if (offset + index < data.size()) {
            value |= static_cast<std::uint32_t>(data[offset + index]) << (index * 8U);
        }
    }
    return value;
}

[[nodiscard]] auto make_request(std::span<const std::uint8_t> data) -> next::clip_request64 {
    const auto seed = read_u32(data, 0);
    const auto mode = data.empty() ? std::uint8_t{} : data[0];
    support::random_path_generator generator{seed};

    next::clip_request64 request;
    request.clip_type = generator.clip_type();
    request.fill_rule = generator.fill_rule();

    const auto max_complexity = 4 + static_cast<int>(read_u32(data, 4) % 48U);
    const auto subject_count = 1 + static_cast<int>(read_u32(data, 8) % 4U);
    const auto clip_count = static_cast<int>(read_u32(data, 12) % 4U);

    request.subjects = generator.paths(subject_count, max_complexity);
    request.clips = generator.paths(clip_count, max_complexity);
    if ((mode & 0x01U) != 0U) {
        request.open_subjects =
            generator.paths(static_cast<int>(read_u32(data, 16) % 3U), max_complexity / 2);
    }
    return request;
}

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* raw_data, std::size_t size) -> int {
    const auto data = std::span<const std::uint8_t>{raw_data, size};
    if (data.size() < 4) { return 0; }

    try {
        auto request = make_request(data);
        const auto scalar = next::clip(request);
        static_cast<void>(scalar);

        std::vector<next::clip_request64> batch_requests;
        batch_requests.push_back(std::move(request));
        batch_requests.push_back(make_request(data.subspan(1)));
        const auto batch = next::clip_batch(batch_requests);
        static_cast<void>(batch);
    } catch (const std::exception&) { return 0; }

    return 0;
}
