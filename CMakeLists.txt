# =============================================================================
#  Drapeo — logiciel de création de vêtements 3D
#  Étape 1 : prototype « tissu qui tombe sur une sphère »
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(Drapeo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(DRAPEO_BUILD_APP   "Construire l'application graphique (GLFW + ImGui)" ON)
option(DRAPEO_BUILD_TESTS "Construire les tests unitaires" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# --- glm : mathématiques (vecteurs, matrices) --------------------------------
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(glm)

# --- ufbx : lecture FBX et OBJ (un seul fichier C, licence MIT / domaine public)
FetchContent_Declare(ufbx
  GIT_REPOSITORY https://github.com/ufbx/ufbx.git
  GIT_TAG        v0.21.5
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(ufbx)
# --- CDT : triangulation de Delaunay contrainte (en-têtes seulement, MPL-2.0)
FetchContent_Declare(cdt
  GIT_REPOSITORY https://github.com/artem-ogre/CDT.git
  GIT_TAG        1.4.1
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _aucun_)   # on n'utilise que les en-têtes
FetchContent_MakeAvailable(cdt)

add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx PUBLIC ${ufbx_SOURCE_DIR})

# --- Cœur sans dépendance graphique : geometry + simulation ------------------
# Ce module ne connaît ni OpenGL ni l'interface : il est testable seul.
add_library(drapeo_core STATIC
  src/geometry/ClothMesh.cpp
  src/geometry/TriangleBvh.cpp
  src/io/MeshImport.cpp
  src/io/PatternImport.cpp
  src/io/GarmentExport.cpp
  src/io/RasterImage.cpp
  src/io/PdfImport.cpp
  src/io/ProjectFile.cpp
  src/io/Settings.cpp
  src/garment/BodyAxes.cpp
  src/geometry/Mannequin.cpp
  src/garment/Pattern.cpp
  src/garment/PatternOps.cpp
  src/garment/GarmentMesher.cpp
  src/simulation/SpatialHash.cpp
  src/simulation/XpbdSolver.cpp
  src/simulation/Materials.cpp)
target_include_directories(drapeo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src PRIVATE ${cdt_SOURCE_DIR}/CDT/include
                                                ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
target_link_libraries(drapeo_core PUBLIC glm::glm PRIVATE ufbx)
if(MSVC)
  target_compile_options(drapeo_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(drapeo_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Application ------------------------------------------------------------
if(DRAPEO_BUILD_APP)
  set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
  if(UNIX AND NOT APPLE)
    # GLFW 3.4 active Wayland par défaut ; on le désactive si les outils manquent (X11 suffit).
    find_program(DRAPEO_WAYLAND_SCANNER wayland-scanner)
    if(NOT DRAPEO_WAYLAND_SCANNER)
      set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    endif()
  endif()
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(glfw imgui)   # imgui n'a pas de CMakeLists : simple téléchargement

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC glfw)

  find_package(OpenGL REQUIRED)

  add_executable(drapeo
    src/app/main.cpp
    src/app/App.cpp
    src/app/FileDialog.cpp
    src/ui/PatternEditor.cpp
    src/rendering/GlFunctions.cpp
    src/rendering/Shader.cpp
    src/rendering/Camera.cpp
    src/rendering/Renderer.cpp)
  target_link_libraries(drapeo PRIVATE drapeo_core imgui glfw OpenGL::GL)
  if(WIN32)
    # Boîte de dialogue « Ouvrir » native ; pas de console noire au lancement.
    target_link_libraries(drapeo PRIVATE comdlg32)
    target_sources(drapeo PRIVATE src/app/drapeo.rc)   # icône de l'exécutable
    set_target_properties(drapeo PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
      target_link_options(drapeo PRIVATE /ENTRY:mainCRTStartup)
    endif()
  endif()
  target_compile_definitions(drapeo PRIVATE GLFW_INCLUDE_NONE)
  if(APPLE)
    target_compile_definitions(drapeo PRIVATE GL_SILENCE_DEPRECATION)
  endif()
endif()

# --- Tests ------------------------------------------------------------------
if(DRAPEO_BUILD_TESTS)
  enable_testing()
  add_executable(drapeo_tests tests/test_main.cpp)
  target_link_libraries(drapeo_tests PRIVATE drapeo_core)
  target_compile_definitions(drapeo_tests PRIVATE DRAPEO_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  add_test(NAME drapeo_tests COMMAND drapeo_tests)
endif()
