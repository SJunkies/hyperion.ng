#pragma once

// stl includes
#include <string>

// Qt includes
#include <QByteArray>
#include <QTcpSocket>
#include <QStringList>
#include <QString>

// Hyperion includes
#include <hyperion/Hyperion.h>

//Utils includes
#include <utils/VideoMode.h>

// proto includes
#include "hyperion_reply_generated.h"
#include "hyperion_request_generated.h"
#include "protoserver/ProtoConnection.h"

///
/// The Connection object created by a ProtoServer when a new connection is established
///
class ProtoClientConnection : public QObject
{
	Q_OBJECT

public:
	///
	/// Constructor
	/// @param socket The Socket object for this connection
	///
	ProtoClientConnection(QTcpSocket * socket);

	///
	/// Destructor
	///
	~ProtoClientConnection();

public slots:
	///
	/// Send video mode message to connected client
	/// @param videoMode the video mode to be set
	///
	void setVideoMode(const VideoMode videoMode);

signals:
	///
	/// Signal which is emitted when the connection is being closed
	/// @param connection This connection object
	///
	void connectionClosed(ProtoClientConnection * connection);
	void newMessage(const uint8_t* msgData, uint32_t messageSize);

private slots:
	///
	/// Slot called when new data has arrived
	///
	void readData();

	///
	/// Slot called when this connection is being closed
	///
	void socketClosed();

private:
	///
	/// Handle an incoming request
	///
	/// @param req the incoming request
	///
	void handleMessage(const hyperionnet::Request *req);

	///
	/// Handle an incoming Color message
	///
	/// @param color incoming data
	///
	void handleColorCommand(const hyperionnet::Color * color);

	///
	/// Handle an incoming Image message
	///
	/// @param image incoming data
	///
	void handleImageCommand(const hyperionnet::Image * image);

	///
	/// Handle an incoming Clear message
	///
	/// @param clear incoming data
	///
	void handleClearCommand(const hyperionnet::Clear * clear);

	///
	/// Handle an incoming Register message
	///
	/// @param message the incoming message
	///
	void handleRegisterCommand(const hyperionnet::Register *reg);

	///
	/// Handle an incoming message of unknown type
	///
	void handleNotImplemented();

	///
	/// Send a message to the connected client
	///
	void sendMessage();

	///
	/// Send a standard reply indicating success
	///
	void sendSuccessReply();

	///
	/// Send an error message back to the client
	///
	/// @param error String describing the error
	///
	void sendErrorReply(const std::string & error);

private:
	/// The TCP-Socket that is connected tot the Proto-client
	QTcpSocket * _socket;

	/// Link to Hyperion for writing led-values to a priority channel
	Hyperion * _hyperion;

	int _priority;

	/// address of client
	QString _clientAddress;

	// Flatbuffers builder
	flatbuffers::FlatBufferBuilder builder;
};
