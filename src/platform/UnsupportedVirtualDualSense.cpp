#include "dualsense/VirtualDualSense.h"

namespace asb::dualsense {
namespace {

class UnsupportedVirtualDualSense final : public VirtualDualSense {
public:
    bool open(std::string& error, FeedbackHandler) override {
        error = "The Virtual DualSense backend is only available on Windows.";
        return false;
    }

    void close() noexcept override {}
    bool updateInput(const DualSenseInputState&, std::string& error) override {
        error = "The Virtual DualSense backend is only available on Windows.";
        return false;
    }
    bool connected() const noexcept override { return false; }

    VirtualDualSenseStats stats() const override {
        return {};
    }
};

} // namespace

std::unique_ptr<VirtualDualSense> createVirtualDualSense(VirtualDualSenseOptions) {
    return std::make_unique<UnsupportedVirtualDualSense>();
}

} // namespace asb::dualsense
