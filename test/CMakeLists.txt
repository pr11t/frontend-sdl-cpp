add_executable(ProjectMSDL-NetworkTests
        NetworkApiTest.cpp
        )

target_link_libraries(ProjectMSDL-NetworkTests
        PRIVATE
        ProjectMSDL-Network
        Poco::Net
        Poco::JSON
        )

add_test(NAME ProjectMSDL-NetworkTests COMMAND ProjectMSDL-NetworkTests)
