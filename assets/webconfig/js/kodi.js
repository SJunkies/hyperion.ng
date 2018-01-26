
var kodiAPI = {
    _socket: null,
    _addr: null,
    _onConnectListeners: [],
    _activePlayerList: [],
    resultCallback: null,
    _lastAction: null,
    _playerid: null,
    DEFAULT_TCP_PORT: "9090",

    getKodiWebsocketConnString: function() {
        let address = getStorage("kodiAddress"); // could be only an ip or a combination from x.x.x.x:yyyy
        let hasPort = address.indexOf(":") != -1;
        
        if (!hasPort) {
            return 'ws://'+address+':'+DEFAULT_TCP_PORT;
        }
        
        return 'ws://'+address;
    },
    connectionParamsChanged: function() {
        return this._addr != this.getKodiWebsocketConnString();
    },
    connect: function(connectCallback){
        if (!window.WebSocket) {
            $(hyperion).trigger("error");
            alert("Websocket is not supported by your browser");
            return;
        }

        if (this._socket) {
            console.log("reset socket");
            this.disconnect();
        }

        this._addr = this.getKodiWebsocketConnString();

        if (this._addr == null) {
            return false;
        }

        if (connectCallback != null) {
            this._onConnectListeners.push(connectCallback);
        }

        this._socket = new WebSocket(this._addr);
        this._socket.onopen = this._onopen.bind(this);;
        this._socket.onmessage = this._onmessage.bind(this);;
        this._socket.onclose = this._onclose.bind(this);;
        this._socket.onerror = this._onerror.bind(this);
    },
    disconnect: function() {
        if (!this._socket) {
            return;
        }
        this._socket.onclose = null;
        this._socket.onerror = null;
        this._socket.close()
        this._reset();
    },

    _reset: function() {
        this._socket = null;
        this._addr = null;
        this._onConnectListeners = [];
        this._activePlayerList = [];
        this.resultCallback = null;
        this._lastAction = null;
        this._playerid = null;
    },

    _onopen: function () {
        this._triggerCallback("connected")
    },

    _onmessage: function (message) {
        var msg = JSON.parse( message.data );

        if (this._lastAction == "Player.Open") {
            if (msg.method == "Player.OnStop") {
                /* If we already have a valid player id we can stop here
                   if not we continue and wait until the OnPlay triggers. 
                   This avoids the extra cost of calling stopAllActivePlayers */
                if (this._playerid) {
                    this._triggerCallback("ok");
                }
                return; 
            } else if (msg.method != "Player.OnPlay") {
                return;
            }
            // We are using a new player lets save its id.
            this._playerid = msg.params.data.player.playerid;
            this._triggerCallback("ok");
        } else if (this._lastAction == "Player.Stop") {
            if (msg.method != "Player.OnStop") {
                return; // ignore useless data
            }
            this._playerid = null;
            this._triggerCallback("ok");
        } else if (msg.id == 1){
            if (this._lastAction == "Player.GetActivePlayers") {
                this._activePlayerList = msg.result;
                this._triggerCallback("ok");
            } else if (msg.result == true || msg.result == "OK") {
                this._triggerCallback("ok");
            }
        }
    },

    _onclose: function () {
        this._triggerCallback("error")
        this._reset();
    },

    _onerror: function (e) {
        this._triggerCallback("error")
        this._reset();
    },

    _send: function (message, cb) {
        if (!this._socket) {
            return;
        }

        if (this._socket.readyState !== 1) {
            if (cb != null) {
                this._onConnectListeners.push(cb)
            }
            return;
        } 
        
        this._lastAction = message.method;
        this.resultCallback = cb;
        this._socket.send(JSON.stringify(message));
    },
    _triggerCallback: function(msg) {
        this._lastAction = null;

        if (this.resultCallback == null) {
            while (this._onConnectListeners.length > 0) {
               this._onConnectListeners.pop()(msg);
            }
            return;
        }

        this.resultCallback(msg);
        this.resultCallback = null;
    },
    _checkReconnect: function() {
        if (this._socket == null || this.connectionParamsChanged()) {
            this.connect();
        }
    },
    _prepareRequest: function() {
        return {jsonrpc:"2.0", id: 1};
    },
    _preparePlayerOpenRequest: function(url) {
        var playReq = this._prepareRequest();
        playReq.method = "Player.Open";
        playReq.params = {item:{file: url}};
        return playReq;
    },
    // API Methods
    sendMedia: function(url, cb) {
        this._checkReconnect();
        this._send(this._preparePlayerOpenRequest(url), cb);
    },
    sendMessage: function(title,msg, type, time, cb) {
        this._checkReconnect();
        var msgReq = this._prepareRequest();
        msgReq.method = "GUI.ShowNotification";
        msgReq.params = {title: title, message: msg, image:type, displaytime: time};
        this._send(msgReq, cb);
    },
    sendStop: function(playerid,cb) {
        this._checkReconnect();
        var stopReq =  this._prepareRequest();
        stopReq.method = "Player.Stop";
        if (!playerid) {
            if (!this._playerid) {
                this.stopAllActivePlayers(cb);
                return;
            }
            playerid = this._playerid;
        }
        stopReq.params = {playerid: playerid};
        this._send(stopReq, cb);
    },
    sendRotate: function(playerid, cb) {
        this._checkReconnect();
        var rotateReq =  this._prepareRequest();
        rotateReq.method = "Player.Rotate";
        rotateReq.params = {playerid: playerid};
        this._send(rotateReq, cb);
    },
    getAllActivePlayers: function(cb) {
        var activePlayerReq =  this._prepareRequest();
        activePlayerReq.method = "Player.GetActivePlayers";
        this._send(activePlayerReq, cb);
    },
    stopAllActivePlayers: function(cb) {
        this.getAllActivePlayers(function(msg){
            if (msg != "ok") {
                console.log("getAllActivePlayers: failed.");
                return;
            }

            // No players active nothing to stop.
            if (this._activePlayerList.length == 0) {
                if (cb) {
                    cb(msg);
                }
                return;
            }

            for (var i in this._activePlayerList) {
                let playerid = this._activePlayerList[i].playerid;

                if (i == this._activePlayerList.length - 1) {
                    this.sendStop(playerid, cb);
                } else {
                    this.sendStop(playerid);
                }
            }
        }.bind(this));
    }

}