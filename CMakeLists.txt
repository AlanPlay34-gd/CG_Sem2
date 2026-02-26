cmake_minimum_required(VERSION 3.31)
project(CG_Sem2)

set(CMAKE_CXX_STANDARD 20)

add_definitions(-DUNICODE -D_UNICODE)

add_executable(CG_Sem2
        main.cpp
        d3dUtil.cpp
        d3dUtil.h
        DirectXApp.cpp
        DirectXApp.h
        InputDevice.cpp
        InputDevice.h
        Material.h
        MathHelper.cpp
        MathHelper.h
        ObjectConstants.cpp
        ObjectConstants.h
        Parser.cpp
        Parser.h
        Submesh.h
        TgaLoader.cpp
        TgaLoader.h
        ThrowIfFailed.cpp
        ThrowIfFailed.h
        Timer.cpp
        Timer.h
        UploadBuffer.cpp
        UploadBuffer.h
        Vertex.cpp
        Vertex.h
        Window.cpp
        Window.h
)
