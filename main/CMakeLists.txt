idf_component_register(
    SRCS "openiris_main.cpp"
    INCLUDE_DIRS 
        "."
        "../components/openiris_logo/openiris_logo"
    REQUIRES 
        Monitoring 
        nvs_flash 
        openiris_logo 
        wifiManager 
        ProjectConfig 
        StateManager 
        LEDManager 
        MDNSManager 
        CameraManager 
        WebSocketLogger 
        StreamServer 
        CommandManager 
        SerialManager 
        RestAPI 
        Helpers 
        UVCStream
)
component_compile_options(-Wno-error=format= -Wno-format)
