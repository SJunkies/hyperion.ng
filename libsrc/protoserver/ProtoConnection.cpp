// stl includes
#include <stdexcept>

// Qt includes
#include <QRgb>

// protoserver includes
#include "protoserver/ProtoConnection.h"

ProtoConnection::ProtoConnection(const QString & address) :
	_socket(),
	_skipReply(false),
	_prevSocketState(QAbstractSocket::UnconnectedState),
	_log(Logger::getInstance("PROTOCONNECTION"))
	{
	QStringList parts = address.split(":");
	if (parts.size() != 2)
	{
		throw std::runtime_error(QString("PROTOCONNECTION ERROR: Wrong address: Unable to parse address (%1)").arg(address).toStdString());
	}
	_host = parts[0];

	bool ok;
	_port = parts[1].toUShort(&ok);
	if (!ok)
	{
		throw std::runtime_error(QString("PROTOCONNECTION ERROR: Wrong port: Unable to parse the port number (%1)").arg(parts[1]).toStdString());
	}

	// try to connect to host
	Info(_log, "Connecting to Hyperion: %s:%d", _host.toStdString().c_str(), _port);
	connectToHost();

	// start the connection timer
	_timer.setInterval(5000);
	_timer.setSingleShot(false);

	connect(&_timer,SIGNAL(timeout()), this, SLOT(connectToHost()));
	connect(&_socket, SIGNAL(readyRead()), this, SLOT(readData()));
	_timer.start();
}

ProtoConnection::~ProtoConnection()
{
	_timer.stop();
	_socket.close();
}

void ProtoConnection::readData()
{
	qint64 bytesAvail;
	while((bytesAvail = _socket.bytesAvailable()))
	{
		// ignore until we get 4 bytes.
		if (bytesAvail < 4) {
			continue;
		}

		char sizeBuf[4];
		 _socket.read(sizeBuf, sizeof(sizeBuf));

		uint32_t messageSize =
			((sizeBuf[0]<<24) & 0xFF000000) |
			((sizeBuf[1]<<16) & 0x00FF0000) |
			((sizeBuf[2]<< 8) & 0x0000FF00) |
			((sizeBuf[3]    ) & 0x000000FF);

		QByteArray buffer;
		while((uint32_t)buffer.size() < messageSize)
		{
			_socket.waitForReadyRead();
			buffer.append(_socket.read(messageSize - buffer.size()));
		}

		const uint8_t* replyData = reinterpret_cast<const uint8_t*>(buffer.constData());
		flatbuffers::Verifier verifier(replyData, messageSize);

		if (!hyperionnet::VerifyReplyBuffer(verifier))
		{
			Error(_log, "Error while reading data from host");
			return;
		}

		parseReply(hyperionnet::GetReply(replyData));
	}
}

void ProtoConnection::setSkipReply(bool skip)
{
	_skipReply = skip;
}

void ProtoConnection::setColor(const ColorRgb & color, int duration)
{
	auto colorReq = hyperionnet::CreateColor(builder, (color.red << 16) | (color.green << 8) | color.blue, duration);
	auto req = hyperionnet::CreateRequest(builder,hyperionnet::Command_Color, colorReq.Union());

	builder.Finish(req);
	sendMessage(builder.GetBufferPointer(), builder.GetSize());
}

void ProtoConnection::setImage(const Image<ColorRgb> &image, int duration)
{
	auto imgData = builder.CreateVector(reinterpret_cast<const uint8_t*>(image.memptr()), image.size());
	auto rawImg = hyperionnet::CreateRawImage(builder, imgData, image.width(), image.height());
	auto imageReq = hyperionnet::CreateImage(builder, hyperionnet::ImageType_RawImage, rawImg.Union(), duration);
	auto req = hyperionnet::CreateRequest(builder,hyperionnet::Command_Image,imageReq.Union());

	builder.Finish(req);
	sendMessage(builder.GetBufferPointer(), builder.GetSize());
}

void ProtoConnection::clear(int priority)
{
	auto clearReq = hyperionnet::CreateClear(builder, priority);
	auto req = hyperionnet::CreateRequest(builder,hyperionnet::Command_Clear, clearReq.Union());

	builder.Finish(req);
	sendMessage(builder.GetBufferPointer(), builder.GetSize());
}

void ProtoConnection::clearAll()
{
	clear(-1);
}

void ProtoConnection::connectToHost()
{
	// try connection only when
	if (_socket.state() == QAbstractSocket::UnconnectedState)
	{
	   _socket.connectToHost(_host, _port);
	   //_socket.waitForConnected(1000);
	}
}

void ProtoConnection::sendMessage(const uint8_t* buffer, uint32_t size)
{
	// print out connection message only when state is changed
	if (_socket.state() != _prevSocketState )
	{
	  switch (_socket.state() )
	  {
		case QAbstractSocket::UnconnectedState:
		  Info(_log, "No connection to Hyperion: %s:%d", _host.toStdString().c_str(), _port);
		  break;

		case QAbstractSocket::ConnectedState:
		  Info(_log, "Connected to Hyperion: %s:%d", _host.toStdString().c_str(), _port);
		  break;

		default:
		  Debug(_log, "Connecting to Hyperion: %s:%d", _host.toStdString().c_str(), _port);
		  break;
	  }
	  _prevSocketState = _socket.state();
	}


	if (_socket.state() != QAbstractSocket::ConnectedState)
	{
		return;
	}

	const uint8_t header[] = {
		uint8_t((size >> 24) & 0xFF),
		uint8_t((size >> 16) & 0xFF),
		uint8_t((size >>  8) & 0xFF),
		uint8_t((size	  ) & 0xFF)};

	// write message
	int count = 0;
	count += _socket.write(reinterpret_cast<const char *>(header), 4);
	count += _socket.write(reinterpret_cast<const char *>(buffer), size);
	if (!_socket.waitForBytesWritten())
	{
		Error(_log, "Error while writing data to host");
		return;
	}
}

bool ProtoConnection::parseReply(const hyperionnet::Reply *reply)
{
	if (!reply->error()) {
		// no error set must be a success or video
		const auto videoMode = reply->video();
		if (videoMode != -1) {
			// We got a video reply.
			emit setVideoMode(static_cast<VideoMode>(videoMode));
		}
		
		return true;
	}

	return false;
}
