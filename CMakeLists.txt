cmake_minimum_required(VERSION 3.20)
project(CG_Sem2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(-DUNICODE -D_UNICODE)

find_package(assimp REQUIRED)

add_executable(CG_Sem2 WIN32
    src/main.cpp
    src/DirectXApp.cpp
    src/DirectXApp.h
    src/RenderingSystem.cpp
    src/RenderingSystem.h
    src/GBuffer.cpp
    src/GBuffer.h
    src/model_loader.cpp
    src/model_loader.h
    src/mesh_data.h
    src/DDSTextureLoader.cpp
    src/DDSTextureLoader.h
    src/d3dUtil.cpp
    src/d3dUtil.h
    src/UploadBuffer.h
    src/Timer.cpp
    src/Timer.h
    src/Vertex.h
    src/Material.h
    src/Light.h
    src/GameTimer.cpp
    src/GameTimer.h
    shaders/main_shader.hlsl
    shaders/lighting.hlsl
)

target_include_directories(CG_Sem2 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/h
)

target_compile_definitions(CG_Sem2 PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(CG_Sem2 PRIVATE
    d3d12.lib
    dxgi.lib
    d3dcompiler.lib
    windowscodecs.lib
    assimp::assimp
)

if(MSVC)
    target_compile_options(CG_Sem2 PRIVATE /W4 /EHsc)
endif()
