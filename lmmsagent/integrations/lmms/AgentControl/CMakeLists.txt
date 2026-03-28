INCLUDE(BuildPlugin)

BUILD_PLUGIN(AgentControl
    AgentControl.cpp
    AgentControl.h
    AgentControlView.cpp
    AgentControlView.h
    MOCFILES AgentControl.h AgentControlView.h
)

target_link_libraries(AgentControl Qt${QT_VERSION_MAJOR}::Network)
