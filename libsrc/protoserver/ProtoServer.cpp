// system includes
#include <stdexcept>

// qt incl
#include <QTcpServer>

// project includes
#include <hyperion/Hyperion.h>
#include <hyperion/MessageForwarder.h>
#include <protoserver/ProtoServer.h>
#include "protoserver/ProtoConnection.h"
#include "ProtoClientConnection.h"
#include <bonjour/bonjourserviceregister.h>
#include <hyperion/ComponentRegister.h>
#include <utils/NetOrigin.h>

ProtoServer::ProtoServer(const QJsonDocument& config)
	: QObject()
	, _hyperion(Hyperion::getInstance())
	, _server(new QTcpServer(this))
	, _openConnections()
	, _log(Logger::getInstance("PROTOSERVER"))
	, _componentRegister( & _hyperion->getComponentRegister())
	, _netOrigin(NetOrigin::getInstance())
{
	Debug(_log,"Instance created");
	connect( _server, SIGNAL(newConnection()), this, SLOT(newConnection()));
	handleSettingsUpdate(settings::PROTOSERVER, config);

	QStringList slaves = _hyperion->getForwarder()->getProtoSlaves();

	for (const auto& entry : slaves)
	{
		ProtoConnection* p = new ProtoConnection(entry.toLocal8Bit().constData());
		p->setSkipReply(true);
		_proxy_connections << p;
	}

	// listen for component changes
	connect(_componentRegister, &ComponentRegister::updatedComponentState, this, &ProtoServer::componentStateChanged);

	// get inital forwarder state
	componentStateChanged(hyperion::COMP_FORWARDER, _componentRegister->isComponentEnabled(hyperion::COMP_FORWARDER));
}

ProtoServer::~ProtoServer()
{
	foreach (ProtoClientConnection * connection, _openConnections) {
		delete connection;
	}

	while (!_proxy_connections.isEmpty())
		delete _proxy_connections.takeFirst();
}

void ProtoServer::start()
{
	if(_server->isListening())
		return;

	if (!_server->listen(QHostAddress::Any, _port))
	{
		Error(_log,"Could not bind to port '%d', please use an available port",_port);
		return;
	}
	Info(_log, "Started on port %d", _port);

	if(_serviceRegister == nullptr)
	{
		_serviceRegister = new BonjourServiceRegister(this);
		_serviceRegister->registerService("_hyperiond-proto._tcp", _port);
	}
	else if( _serviceRegister->getPort() != _port)
	{
		delete _serviceRegister;
		_serviceRegister = new BonjourServiceRegister(this);
		_serviceRegister->registerService("_hyperiond-proto._tcp", _port);
	}
}

void ProtoServer::stop()
{
	if(!_server->isListening())
		return;

	_server->close();
	Info(_log, "Stopped");
}

void ProtoServer::handleSettingsUpdate(const settings::type& type, const QJsonDocument& config)
{
	if(type == settings::PROTOSERVER)
	{
		QJsonObject obj = config.object();
		if(obj["port"].toInt() != _port)
		{
			_port = obj["port"].toInt();
			stop();
			start();
		}
	}
}

uint16_t ProtoServer::getPort() const
{
	return _port;
}

void ProtoServer::newConnection()
{
	while(_server->hasPendingConnections())
	{
		if(QTcpSocket * socket = _server->nextPendingConnection())
		{
			if(_netOrigin->accessAllowed(socket->peerAddress(), socket->localAddress()))
			{
				Debug(_log, "New connection");
				ProtoClientConnection * connection = new ProtoClientConnection(socket);
				_openConnections.insert(connection);

				// register slot for cleaning up after the connection closed
				connect(connection, SIGNAL(connectionClosed(ProtoClientConnection*)), this, SLOT(closedConnection(ProtoClientConnection*)));
				connect(connection, SIGNAL(newMessage(const uint8_t* , uint32_t)), this, SLOT(newMessage(const uint8_t* , uint32_t)));

				// register forward signal for video mode
				connect(this, SIGNAL(videoMode(VideoMode)), connection, SLOT(setVideoMode(VideoMode)));
			}
			else
			{
				socket->close();
			}
		}
	}
}

void ProtoServer::newMessage(const uint8_t* buffer, uint32_t size)
{
	for (int i = 0; i < _proxy_connections.size(); ++i)
		_proxy_connections.at(i)->sendMessage(buffer, size);
}

void ProtoServer::sendImageToProtoSlaves(int priority, const Image<ColorRgb> & image, int duration_ms)
{
	if ( _forwarder_enabled )
	{
		for (int i = 0; i < _proxy_connections.size(); ++i)
			_proxy_connections.at(i)->setImage(image, priority, duration_ms);
	}
}

void ProtoServer::componentStateChanged(const hyperion::Components component, bool enable)
{
	if (component == hyperion::COMP_FORWARDER)
	{
		_forwarder_enabled = enable;
	}
}

void ProtoServer::closedConnection(ProtoClientConnection *connection)
{
	Debug(_log, "Connection closed");
	_openConnections.remove(connection);

	// schedule to delete the connection object
	connection->deleteLater();
}
