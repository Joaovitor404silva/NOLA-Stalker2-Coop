cmake_minimum_required(VERSION 3.18)

set(TARGET NolaNative)
project(${TARGET})

option(S2X_RELEASE "one-file release build" OFF)

set(S2N_SOURCES "dllmain.cpp" "s2n_ai.cpp" "s2n_nat.cpp" "s2n_save.cpp" "s2n_join.cpp" "s2n_rel.cpp")
set(S2X_SOURCES "s2x_flags.cpp" "s2x_io.cpp" "s2x_bridge.cpp" "s2x_sched.cpp" "s2x_ref.cpp" "s2x_lua.cpp" "s2x_ue.cpp" "s2x_nat.cpp" "s2x_istate.cpp" "s2x_istate_nat.cpp" "s2x_ui.cpp" "s2x_tut.cpp" "s2x_uiq.cpp" "s2x_buildchk.cpp" "s2x_data.rc")
set(S2M_SOURCES "s2m_bodies.cpp" "s2m_damage.cpp" "s2m_life_x0.cpp" "s2m_items.cpp" "s2m_items_x0.cpp" "s2m_world.cpp" "s2m_world_x0.cpp" "s2m_pop.cpp" "s2m_pop_x0.cpp"
    "s2m_quest.cpp" "s2m_lock.cpp" "s2m_save.cpp" "s2m_save_x0.cpp" "s2m_travel.cpp" "s2m_travel_x0.cpp" "s2m_session.cpp" "s2m_session_x0.cpp"
    "s2m_wshot.cpp" "s2m_throw.cpp" "s2m_aiwatch.cpp" "s2m_console.cpp" "s2m_loadscreen.cpp" "s2m_solo.cpp" "s2m_menu.cpp" "s2m_csave.cpp" "s2m_pid.cpp" "s2m_savefile.cpp" "s2m_wpkg.cpp" "s2m_wsplice.cpp" "s2m_strip.cpp" "s2m_oodle.cpp" "s2m_lshold.cpp" "s2m_ldmgr.cpp" "s2m_cgc_x0.cpp" "s2m_cgcmc_x0.cpp" "s2m_ixtap_x0.cpp" "s2m_qbranch.cpp" "s2m_text.cpp" "s2m_census.cpp" "s2m_pcompass.cpp" "s2m_qobj.cpp" "s2m_lockbehind.cpp")
set(S2R_SOURCES "s2r_net.cpp" "s2r_x0.cpp" "s2r_steam.cpp" "s2r_steam_x0.cpp")

if(NOT S2X_RELEASE)
    list(APPEND S2X_SOURCES "s2x_fr.cpp")
endif()
if(NOT S2X_RELEASE)
    list(APPEND S2X_SOURCES "s2x_hot.cpp")
endif()

set(S2X_DATA_RC "${CMAKE_CURRENT_SOURCE_DIR}/s2x_data.rc")
file(STRINGS "${S2X_DATA_RC}" S2X_DATA_LINES REGEX "RCDATA")
set(S2X_DATA_FILES "")
foreach(line ${S2X_DATA_LINES})
    if(line MATCHES "\"([^\"]+)\"")
        string(REPLACE "\\\\" "/" rel "${CMAKE_MATCH_1}")
        get_filename_component(abs "${rel}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND S2X_DATA_FILES "${abs}")
    endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${S2X_DATA_RC}")
set_source_files_properties("${S2X_DATA_RC}" PROPERTIES OBJECT_DEPENDS "${S2X_DATA_FILES}")

add_library(${TARGET} SHARED ${S2N_SOURCES} ${S2X_SOURCES} ${S2M_SOURCES} ${S2R_SOURCES})
target_compile_definitions(${TARGET} PRIVATE NOMINMAX)
if(S2X_RELEASE)
    target_compile_definitions(${TARGET} PRIVATE S2X_RELEASE)
else()
    target_compile_definitions(${TARGET} PRIVATE S2X_DEVCMD S2X_FR)
endif()
target_include_directories(${TARGET} PRIVATE .)
target_link_libraries(${TARGET} PUBLIC UE4SS ws2_32 user32)
target_link_options(${TARGET} PRIVATE "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${TARGET}>")
