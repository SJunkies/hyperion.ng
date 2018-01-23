// system includes
#include <stdexcept>
#include <cassert>

// stl includes
#include <iostream>
#include <sstream>
#include <iterator>

// Qt includes
#include <QRgb>
#include <QResource>
#include <QDateTime>
#include <QHostInfo>

// hyperion util includes
#include "utils/ColorRgb.h"

// project includes
#include "ProtoClientConnection.h"

ProtoClientConnection::ProtoClientConnection(QTcpSocket *socket)
	: QObject()
	, _socket(socket)
	, _hyperion(Hyperion::getInstance())
	, _priority(-1)
	, _clientAddress(QHostInfo::fromName(socket->peerAddress().toString()).hostName())
{
	// connect internal signals and slots
	connect(_socket, SIGNAL(disconnected()), this, SLOT(socketClosed()));
	connect(_socket, SIGNAL(readyRead()), this, SLOT(readData()));
}

ProtoClientConnection::~ProtoClientConnection()
{
	delete _socket;
}

void ProtoClientConnection::readData()
{
	qint64 bytesAvail;
	while((bytesAvail = _socket->bytesAvailable()))
	{
		// ignore until we get 4 bytes.
		if (bytesAvail < 4) {
			continue;
		}

		char sizeBuf[4];
		 _socket->read(sizeBuf, sizeof(sizeBuf));

		uint32_t messageSize =
			((sizeBuf[0]<<24) & 0xFF000000) |
			((sizeBuf[1]<<16) & 0x00FF0000) |
			((sizeBuf[2]<< 8) & 0x0000FF00) |
			((sizeBuf[3]    ) & 0x000000FF);

		QByteArray buffer;
		while((uint32_t)buffer.size() < messageSize)
		{
			_socket->waitForReadyRead();
			buffer.append(_socket->read(messageSize - buffer.size()));
		}

		const uint8_t* msgData = reinterpret_cast<const uint8_t*>(buffer.constData());
		flatbuffers::Verifier verifier(msgData, messageSize);

		if (!proto::VerifyHyperionRequestBuffer(verifier))
		{
			sendErrorReply("Unable to parse message");
			return;
		}

		auto message = proto::GetHyperionRequest(msgData);

		handleMessage(message);
		emit newMessage(msgData,messageSize);
	}
}

void ProtoClientConnection::socketClosed()
{
	_hyperion->clear(_priority);
	emit connectionClosed(this);
}

void ProtoClientConnection::setVideoMode(const VideoMode videoMode)
{
	proto::HyperionReplyBuilder replyBuilder(builder);
	replyBuilder.add_type(proto::Type_VIDEO);
	replyBuilder.add_video((int)videoMode);

	auto reply = replyBuilder.Finish();
	
	builder.Finish(reply);
}

void ProtoClientConnection::handleMessage(const proto::HyperionRequest * message)
{
	switch (message->command())
	{
	case proto::Command_COLOR:
		if (!flatbuffers::IsFieldPresent(message, proto::HyperionRequest::VT_COLORREQUEST))
		{
			sendErrorReply("Received COLOR command without ColorRequest");
			break;
		}
		handleColorCommand(message->colorRequest());
		break;
	case proto::Command_IMAGE:
		if (!flatbuffers::IsFieldPresent(message, proto::HyperionRequest::VT_IMAGEREQUEST))
		{
			sendErrorReply("Received IMAGE command without ImageRequest");
			break;
		}
		handleImageCommand(message->imageRequest());
		break;
	case proto::Command_CLEAR:
		if (!flatbuffers::IsFieldPresent(message, proto::HyperionRequest::VT_CLEARREQUEST))
		{
			sendErrorReply("Received CLEAR command without ClearRequest");
			break;
		}
		handleClearCommand(message->clearRequest());
		break;
	case proto::Command_CLEARALL:
		handleClearallCommand();
		break;
	default:
		handleNotImplemented();
	}
}

void ProtoClientConnection::handleColorCommand(const proto::ColorRequest *message)
{
	// extract parameters
	int priority = message->priority();
	int duration = message->duration();
	ColorRgb color;
	color.red = qRed(message->RgbColor());
	color.green = qGreen(message->RgbColor());
	color.blue = qBlue(message->RgbColor());

	// make sure the prio is registered before setColor()
	if(priority != _priority)
	{
		_hyperion->clear(_priority);
		_hyperion->registerInput(priority, hyperion::COMP_PROTOSERVER, "proto@"+_clientAddress);
		_priority = priority;
	}

	// set output
	_hyperion->setColor(_priority, color, duration);

	// send reply
	sendSuccessReply();
}

void ProtoClientConnection::handleImageCommand(const proto::ImageRequest *message)
{
	// extract parameters
	int priority = message->priority();
	int duration = message->duration();
	int width = message->imagewidth();
	int height = message->imageheight();
	const auto & imageData = message->imagedata();

	// make sure the prio is registered before setInput()
	if(priority != _priority)
	{
		_hyperion->clear(_priority);
		_hyperion->registerInput(priority, hyperion::COMP_PROTOSERVER, "proto@"+_clientAddress);
		_priority = priority;
	}

	// check consistency of the size of the received data
	if ((int) imageData->size() != width*height*3)
	{
		sendErrorReply("Size of image data does not match with the width and height");
		return;
	}

	// create ImageRgb
	Image<ColorRgb> image(width, height);
	memcpy(image.memptr(), imageData->data(), imageData->size());

	_hyperion->setInputImage(_priority, image, duration);

	// send reply
	sendSuccessReply();
}


void ProtoClientConnection::handleClearCommand(const proto::ClearRequest *message)
{
	// extract parameters
	int priority = message->priority();

	// clear priority
	_hyperion->clear(priority);
	// send reply
	sendSuccessReply();
}

void ProtoClientConnection::handleClearallCommand()
{
	// clear priority
	_hyperion->clearall();

	// send reply
	sendSuccessReply();
}


void ProtoClientConnection::handleNotImplemented()
{
	sendErrorReply("Command not implemented");
}

void ProtoClientConnection::sendMessage()
{
	auto size = builder.GetSize();
	const uint8_t* buffer = builder.GetBufferPointer();
	uint8_t sizeData[] = {uint8_t(size >> 24), uint8_t(size >> 16), uint8_t(size >> 8), uint8_t(size)};
	_socket->write((const char *) sizeData, sizeof(sizeData));
	_socket->write((const char *)buffer, size);
	_socket->flush();
	builder.Clear();
}

void ProtoClientConnection::sendSuccessReply()
{
	auto reply = proto::CreateHyperionReplyDirect(builder, proto::Type_REPLY, true);
	builder.Finish(reply);

	// send reply
	sendMessage();
}

void ProtoClientConnection::sendErrorReply(const std::string &error)
{
	// create reply
	auto reply = proto::CreateHyperionReplyDirect(builder, proto::Type_REPLY, false, error.c_str());
	builder.Finish(reply);

	// send reply
	sendMessage();
}
