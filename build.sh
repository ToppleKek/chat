set -e

#-Wall -Wextra -Wpedantic -Wconversion
CXX_FLAGS="-g -std=c++20 -Wall -Wextra -Wno-unused-variable -Xlinker /SUBSYSTEM:CONSOLE -Xlinker /NODEFAULTLIB:MSVCRTD"
CXX_FILES="server/main.cpp server/win32_chat_server.cpp"
CXX_FILES_CLIENT="client/main.cpp client/win32_chat_client.cpp client/vulkan.cpp ./thirdparty/imgui/imgui.cpp ./thirdparty/imgui/imgui_draw.cpp ./thirdparty/imgui/imgui_tables.cpp ./thirdparty/imgui/imgui_widgets.cpp ./thirdparty/imgui/imgui_impl_win32.cpp ./thirdparty/imgui/imgui_impl_vulkan.cpp ./thirdparty/imgui/imgui_demo.cpp"
LIBS="user32 ${VULKAN_SDK}/Lib/vulkan-1.lib -lgdi32 -lwinmm -lsetupapi -loleaut32 -lole32 -limm32.lib -lversion -ladvapi32 -lshell32 -lWs2_32 -lSecur32 -lBcrypt -lole32 -ldxguid -lMfplat -lMfuuid -lstrmiids -ldsound"
# LIBS="user32  ${VULKAN_SDK}/Lib/vulkan-1.lib -lgdi32 -lwinmm -lsetupapi -loleaut32 -lole32 -limm32.lib -lversion -ladvapi32 -lshell32 -lWs2_32 -lSecur32 -lBcrypt -lole32 -ldxguid -lMfplat -lMfuuid -lstrmiids -ldsound ./lib/libavcodec.a ./lib/libavformat.a ./lib/libavutil.a ./lib/libswresample.a ./lib/SDL2main.lib ./lib/SDL2main.lib ./lib/SDL2.lib"
#./lib/libavcodec.a ./lib/libavformat.a ./lib/libavutil.a ./lib/libswresample.a
EXE_NAME="chat.exe"
CLIENT_EXE_NAME="chat_client.exe"
INCLUDE="thirdparty/include -I ${VULKAN_SDK}/Include"

mkdir -p build

if [ "${1}" = "run" ]; then
    cd build
    ./$EXE_NAME
    exit 0
fi

if [ "${1}" = "runc" ]; then
    cd build
    ./$CLIENT_EXE_NAME
    exit 0
fi

if [ "${1}" = "brc" ]; then
    clang ${CXX_FLAGS} -l ${LIBS} -I ${INCLUDE} ${CXX_FILES_CLIENT} -o build/${CLIENT_EXE_NAME}
    cd build
    ./$CLIENT_EXE_NAME
    exit 0
fi

if [ "${1}" = "br" ]; then
    clang ${CXX_FLAGS} -l ${LIBS} -I ${INCLUDE} ${CXX_FILES} -o build/${EXE_NAME}
    cd build
    ./$EXE_NAME
    exit 0
fi

if [ "${1}" = "shader" ]; then
    glslc shaders/main.frag -o build/frag.spv
    glslc shaders/main.vert -o build/vert.spv
    exit 0
fi

# clang ${CXX_FLAGS} -l ${LIBS} -I ${INCLUDE} ${CXX_FILES} -o ${EXE_NAME}
clang ${CXX_FLAGS} -l ${LIBS} -I ${INCLUDE} ${CXX_FILES} -o build/${EXE_NAME}
clang ${CXX_FLAGS} -l ${LIBS} -I ${INCLUDE} ${CXX_FILES_CLIENT} -o build/${CLIENT_EXE_NAME}

