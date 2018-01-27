// protoserver includes
#include "protoserver/ProtoConnectionWrapper.h"

ProtoConnectionWrapper::ProtoConnectionWrapper(const QString &address,
											   int priority,
											   int duration_ms,
											   bool skipProtoReply)
	: _priority(priority)
	, _duration_ms(duration_ms)
	, _connection(address)
{
	_connection.setSkipReply(skipProtoReply);
	// We should probably register with the ProtoConnection here <todo> <flatbuffers>
	connect(&_connection, SIGNAL(setVideoMode(VideoMode)), this, SIGNAL(setVideoMode(VideoMode)));
}

ProtoConnectionWrapper::~ProtoConnectionWrapper()
{
}

void ProtoConnectionWrapper::receiveImage(const Image<ColorRgb> & image)
{
	// Skip the priority sending for now <todo> <flatbuffers>
	_connection.setImage(image, _duration_ms);
}
