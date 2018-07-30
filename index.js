const binding = require('node-gyp-build')(__dirname)
const set = require('unordered-set')
const util = require('util')
const events = require('events')
const dns = require('dns')

const EMPTY = Buffer.alloc(0)

module.exports = UDP

function UDP () {
  if (!(this instanceof UDP)) return new UDP()
  events.EventEmitter.call(this)

  this._sending = []
  this._sent = []
  this._offset = 0
  this._buffer = Buffer.allocUnsafe(2 * 65536)
  this._handle = Buffer.alloc(binding.sizeof_utp_napi_t)
  this._address = null
  this._inited = false
  this._refed = true
  this._closing = false
  this._closed = false
}

util.inherits(UDP, events.EventEmitter)

UDP.prototype._init = function () {
  this._inited = true

  binding.utp_napi_init(this._handle, this,
    this._buffer,
    this._onmessage,
    this._onsend,
    this._onclose
  )

  if (!this._refed) this.unref()
}

UDP.prototype.ref = function () {
  if (this._inited) binding.utp_napi_ref(this._handle)
  this._refed = true
}

UDP.prototype.unref = function () {
  if (this._inited) binding.utp_napi_unref(this._handle)
  this._refed = false
}

UDP.prototype.address = function () {
  if (!this._address) throw new Error('Socket not bound')
  return {
    address: this._address,
    family: 'IPv4',
    port: binding.utp_napi_local_port(this._handle)
  }
}

UDP.prototype.send = function (buf, offset, len, port, host, cb) {
  if (!cb) cb = noop
  if (!isIP(host)) return this._resolveAndSend(buf, offset, len, port, host, cb)
  if (this._closing) return process.nextTick(cb, new Error('Socket is closed'))
  if (!this._address) this.bind(0)

  var send = this._sent.pop()
  if (!send) {
    send = new SendRequest()
    binding.utp_napi_send_request_init(send._handle, send)
  }

  send._index = this._sending.push(send) - 1
  send._buffer = buf
  send._callback = cb

  binding.utp_napi_send(this._handle, send._handle, send._buffer, offset, len, port, host)
}

UDP.prototype._resolveAndSend = function (buf, offset, len, port, host, cb) {
  const self = this

  dns.lookup(host, onlookup)

  function onlookup (err, ip) {
    if (err) return cb(err)
    if (!ip) return cb(new Error('Could not resolve host'))
    self.send(buf, offset, len, port, ip, cb)
  }
}

UDP.prototype.close = function (onclose) {
  if (onclose) this.once('close', onclose)
  if (this._closing) return
  this.closing = true
  this._closeMaybe()
}

UDP.prototype._closeMaybe = function () {
  if (!this._sending.length && this._inited && !this._closed) {
    this._closed = true
    binding.utp_napi_close(this._handle)
  }
}

UDP.prototype.bind = function (port, ip, onlistening) {
  if (typeof port === 'function') return this.bind(0, null, port)
  if (typeof ip === 'function') return this.bind(port, null, ip)
  if (!port) port = 0
  if (!ip) ip = '0.0.0.0'

  if (!this._inited) this._init()
  if (this._closing) return

  if (this._address) {
    this.emit('error', new Error('Socket already bound'))
    return
  }

  if (onlistening) this.once('listening', onlistening)
  if (!isIP(ip)) return this._resolveAndBind(port, ip)

  this._address = ip
  binding.utp_napi_bind(this._handle, port, ip)
  process.nextTick(emitListening, this)
}

UDP.prototype._resolveAndBind = function (port, host) {
  const self = this

  dns.lookup(host, function (err, ip) {
    if (err) return self.emit('error', err)
    self.bind(port, ip)
  })
}

UDP.prototype._onmessage = function (size, port, address) {
  if (size < 0) {
    this.emit('error', new Error('Read failed (status: ' + size + ')'))
    return EMPTY
  }

  const message = this._buffer.slice(this._offset, this._offset += size)
  this.emit('message', message, {address, family: 'IPv4', port})

  if (this._buffer.length - this._offset <= 65536) {
    this._buffer = Buffer.allocUnsafe(this._buffer.length)
    this._offset = 0
    return this._buffer
  }

  return EMPTY
}

UDP.prototype._onsend = function (send, status) {
  const cb = send._callback

  send._callback = send._buffer = null
  set.remove(this._sending, send)
  this._sent.push(send)
  if (this._closing) this._closeMaybe()

  cb(status < 0 ? new Error('Send failed (status: ' + status + ')') : null)
}

UDP.prototype._onclose = function () {
  binding.utp_napi_destroy(this._handle, this._sent.map(toHandle))
  this._handle = null
  this.emit('close')
}

function SendRequest () {
  this._handle = Buffer.alloc(binding.sizeof_utp_napi_send_request_t)
  this._buffer = null
  this._callback = null
  this._index = null
}

function noop () {}

function isIP (ip) {
  return /^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(ip)
}

function toHandle (obj) {
  return obj._handle
}

function emitListening (self) {
  self.emit('listening')
}
