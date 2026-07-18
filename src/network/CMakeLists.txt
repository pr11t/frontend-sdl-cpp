add_library(ProjectMSDL-Network STATIC
        ApiRequestHandler.cpp
        ApiRequestHandler.h
        ControlCommand.h
        ControlCommandQueue.cpp
        ControlCommandQueue.h
        HttpApiServer.cpp
        HttpApiServer.h
        JobRegistry.cpp
        JobRegistry.h
        PresetRepository.cpp
        PresetRepository.h
        VisualState.cpp
        VisualState.h
        )

target_include_directories(ProjectMSDL-Network
        PUBLIC
        "${CMAKE_SOURCE_DIR}/src"
        )

target_link_libraries(ProjectMSDL-Network
        PUBLIC
        Poco::Foundation
        Poco::JSON
        Poco::Net
        Poco::Util
        )
