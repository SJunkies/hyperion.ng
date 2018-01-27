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

		const auto *msgData = reinterpret_cast<const uint8_t*>(buffer.constData());
		flatbuffers::Verifier verifier(msgData, messageSize);

		if (!hyperionnet::VerifyRequestBuffer(verifier))
		{
			sendErrorReply("Unable to parse message");
			return;
		}

		auto message = hyperionnet::GetRequest(msgData);

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
	auto reply = hyperionnet::CreateReplyDirect(builder, nullptr, (int)videoMode);
	builder.Finish(reply);
	sendMessage();
}

void ProtoClientConnection::handleMessage(const hyperionnet::Request * req)
{
	const void* reqPtr;
	if ((reqPtr = req->command_as_Color()) != nullptr) {
		handleColorCommand(static_cast<const hyperionnet::Color*>(reqPtr));
	} else if ((reqPtr = req->command_as_Image()) != nullptr) {
		handleImageCommand(static_cast<const hyperionnet::Image*>(reqPtr));
	} else if ((reqPtr = req->command_as_Clear()) != nullptr) {
		handleClearCommand(static_cast<const hyperionnet::Clear*>(reqPtr));
	} else if ((reqPtr = req->command_as_Register()) != nullptr) {
		handleRegisterCommand(static_cast<const hyperionnet::Register*>(reqPtr));
	} else {
		sendErrorReply("Received invalid packet.");
		handleNotImplemented();
	}
}

void ProtoClientConnection::handleColorCommand(const hyperionnet::Color *colorReq)
{
	// extract parameters
	const int32_t rgbData = colorReq->data();
	ColorRgb color;
	color.red = qRed(rgbData);
	color.green = qGreen(rgbData);
	color.blue = qBlue(rgbData);

	// set output
	_hyperion->setColor(_priority, color, colorReq->duration());

	// send reply
	sendSuccessReply();
}

void ProtoClientConnection::handleRegisterCommand(const hyperionnet::Register *regReq) {
	_priority = regReq->priority();
	_hyperion->registerInput(_priority, hyperion::COMP_PROTOSERVER, regReq->origin()->c_str()+_clientAddress);
}

void ProtoClientConnection::handleImageCommand(const hyperionnet::Image *image)
{
	// extract parameters
	int duration = image->duration();	

	const void* reqPtr;
	if ((reqPtr = image->data_as_RawImage()) != nullptr)
	{
		const auto *img = static_cast<const hyperionnet::RawImage*>(reqPtr);
		const auto & imageData = img->data();
		const int width = img->width();
		const int height = img->height();
		
		if ((int) imageData->size() != width*height*3)
		{
			sendErrorReply("Size of image data does not match with the width and height");
			return;
		}

		Image<ColorRgb> image(width, height);
		memmove(image.memptr(), imageData->data(), imageData->size());
		_hyperion->setInputImage(_priority, image, duration);
	}

	// send reply
	sendSuccessReply();
}


void ProtoClientConnection::handleClearCommand(const hyperionnet::Clear *clear)
{
	// extract parameters
	const int priority = clear->priority();

	if (priority == -1) {
		_hyperion->clearall();
	} 
	else {
		// Check if we are clearing ourselves.
		if (priority == _priority) {
			_priority = -1;
		}

		_hyperion->clear(priority);
	} 

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
	auto reply = hyperionnet::CreateReplyDirect(builder);
	builder.Finish(reply);

	// send reply
	sendMessage();
}

void ProtoClientConnection::sendErrorReply(const std::string &error)
{
	// create reply
	auto reply = hyperionnet::CreateReplyDirect(builder, error.c_str());
	builder.Finish(reply);

	// send reply
	sendMessage();
}
