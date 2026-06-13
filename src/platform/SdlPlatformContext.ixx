export module quader.platform.sdl_platform_context;

export namespace quader {

class SdlPlatformContext final {
public:
    SdlPlatformContext();

    SdlPlatformContext(const SdlPlatformContext&) = delete;
    SdlPlatformContext& operator=(const SdlPlatformContext&) = delete;

    ~SdlPlatformContext();
};

} // namespace quader
