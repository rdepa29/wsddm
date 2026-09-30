cmake_minimum_required(VERSION 3.21)

project(wsddm VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(wsddm WIN32
    src/main.cpp
    src/Sddm.cpp
    src/Sddm.h
    src/ThemeConfig.cpp
    src/ThemeConfig.h
    src/UserModel.cpp
    src/UserModel.h
    src/Log.cpp
    src/Log.h
)

# The theme's QML is used exactly as SDDM ships it, so it is bundled from the qml/
# directory rather than rewritten. The URI prefix must match the load path in main.cpp.
qt_add_qml_module(wsddm
    URI wsddm
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/components/Avatar.qml
        qml/components/BlurWrapper.qml
        qml/components/Greeting.qml
        qml/components/LayerState.qml
        qml/components/Logo.qml
        qml/components/MainClock.qml
        qml/components/MaterialIcon.qml
        qml/components/PasswordInput.qml
        qml/components/RandomQuote.qml
        qml/components/SessionPicker.qml
        qml/components/VideoBackground.qml
        qml/components/shapes/ShapeCanvas.qml
        qml/widgets/CaelestiaFetch.qml
        qml/widgets/SystemButtons.qml
        qml/widgets/WelcomeText.qml
    RESOURCES
        qml/components/shapes/material-shapes.js
        qml/components/shapes/geometry/offset.js
        qml/components/shapes/graphics/matrix.js
        qml/components/shapes/shapes/cubic.js
        qml/components/shapes/shapes/feature.js
        qml/components/shapes/shapes/feature-mapping.js
        qml/components/shapes/shapes/float-mapping.js
        qml/components/shapes/shapes/morph.js
        qml/components/shapes/shapes/point.js
        qml/components/shapes/shapes/polygon-measure.js
        qml/components/shapes/shapes/rounded-corner.js
        qml/components/shapes/shapes/rounded-polygon.js
        qml/components/shapes/shapes/utils.js
        qml/components/shapes/shapes/corner-rounding.js
        qml/config/quotes.json
        qml/assets/avatar.jpg
        qml/assets/dino.png
        qml/assets/google-sans-flex/GoogleSansFlex.ttf
)

target_link_libraries(wsddm PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)

# WIN32_EXECUTABLE is set via the WIN32 argument above, so the CLI commands
# (--set-password, --help) attach to the parent console the same way the C# build did.
target_compile_definitions(wsddm PRIVATE
    QT_DISABLE_DEPRECATED_BEFORE=0x060000
)

