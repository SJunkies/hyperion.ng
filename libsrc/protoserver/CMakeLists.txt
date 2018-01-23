
# Define the current source locations
set(CURRENT_HEADER_DIR ${CMAKE_SOURCE_DIR}/include/protoserver)
set(CURRENT_SOURCE_DIR ${CMAKE_SOURCE_DIR}/libsrc/protoserver)

include_directories(
	${CMAKE_CURRENT_BINARY_DIR}
	${FLATBUFFERS_INCLUDE_DIRS}
)

set(ProtoServer_HEADERS
	hyperion_reply_generated.h
	hyperion_request_generated.h
)

set(ProtoServer_PROTOS
	${CURRENT_SOURCE_DIR}/hyperion_reply.fbs
	${CURRENT_SOURCE_DIR}/hyperion_request.fbs
)

FOREACH(PROTO ${ProtoServer_PROTOS})
	compile_flattbuffer_schema(${PROTO} ${CMAKE_CURRENT_BINARY_DIR})
ENDFOREACH(PROTO)

set_source_files_properties(
	${ProtoServer_HEADERS} PROPERTIES GENERATED TRUE
)


### Split protoclient from protoserver as protoserver relates to HyperionDaemon and standalone capture binarys can't link to it

add_library(protoclient
	${CURRENT_HEADER_DIR}/ProtoConnection.h
	${CURRENT_SOURCE_DIR}/ProtoConnection.cpp
	${CURRENT_HEADER_DIR}/ProtoConnectionWrapper.h
	${CURRENT_SOURCE_DIR}/ProtoConnectionWrapper.cpp
	${CURRENT_SOURCE_DIR}/ProtoClientConnection.h
	${CURRENT_SOURCE_DIR}/ProtoClientConnection.cpp
	${ProtoServer_PROTO_SRCS}
	${ProtoServer_HEADERS}
)

add_library(protoserver
	${CURRENT_HEADER_DIR}/ProtoServer.h
	${CURRENT_SOURCE_DIR}/ProtoServer.cpp
)

target_link_libraries(protoclient
	hyperion
	hyperion-utils
	flatbuffers
	Qt5::Gui
)

target_link_libraries(protoserver
	hyperion
	hyperion-utils
	protoclient
	Qt5::Gui
)
