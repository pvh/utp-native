const binding = require('./binding')
const stream = require('stream')
const util = require('util')
const unordered = require('unordered-set')
const dns = require('dns')

const EMPTY = Buffer.alloc(0)

module.exports = Connection

function Connection (utp, port, address) {
  stream.Duplex.call(this, {highWaterMark: 1024 * 1024})

  this.remoteAddress = address
  this.remoteFamily = 'IPv4'
  this.remotePort = port
  this.destroyed = false

  this._index = -1
  this._utp = utp
  this._handle = Buffer.alloc(binding.sizeof_utp_napi_connection_t)
  this._buffer = Buffer.allocUnsafe(65536 * 2)
  this._offset = 0
  this._view = new Uint32Array(this._handle.buffer, 0, 2)
  this._callback = null
  this._writing = null
  this._error = null
  this._connected = false

  this.on('end', this.destroy)
  this.on('finish', this.destroy)

  binding.utp_napi_connection_init(this._handle, this, this._buffer,
    this._onread,
    this._ondrain,
    this._onend,
    this._onerror,
    this._onclose,
    this._onconnect
  )

  unordered.add(utp.connections, this)
}

util.inherits(Connection, stream.Duplex)

Connection.prototype.setPacketSize = function (size) {
  if (size > 65536) size = 65536
  this._view[0] = size
}

Connection.prototype.address = function () {
  if (this.destroyed) return null
  return this._utp.address()
}

Connection.prototype._read = function () {
  // TODO: backpressure
}

Connection.prototype._write = function (data, enc, cb) {
  if (this.destroyed) return

  if (!this._connected || !binding.utp_napi_connection_write(this._handle, data)) {
    this._callback = cb
    this._writing = new Array(1)
    this._writing[0] = data
    return
  }

  cb(null)
}

Connection.prototype._writev = function (datas, cb) {
  if (this.destroyed) return

  const bufs = new Array(datas.length)
  for (var i = 0; i < datas.length; i++) bufs[i] = datas[i].chunk

  if (bufs.length > 256) return this._write(Buffer.concat(bufs), cb)

  if (!binding.utp_napi_connection_writev(this._handle, bufs)) {
    this._callback = cb
    this._writing = bufs
    return
  }

  cb(null)
}

Connection.prototype._onread = function (size) {
  if (!this._connected) this._onconnect() // makes the server wait for reads before writes

  const buf = this._buffer.slice(this._offset, this._offset += size)

  this.push(buf)

  if (this._buffer.length - this._offset <= 65536) {
    this._buffer = Buffer.allocUnsafe(this._buffer.length)
    this._offset = 0
    return this._buffer
  }

  return EMPTY
}

Connection.prototype._ondrain = function () {
  this._writing = null
  const cb = this._callback
  this._callback = null
  cb(null)
}

Connection.prototype._onclose = function () {
  unordered.remove(this._utp.connections, this)
  this._handle = null
  if (this._error) this.emit('error', this._error)
  this.emit('close')
  this._utp._closeMaybe()
}

Connection.prototype._onerror = function (status) {
  this.destroy(new Error('utp closed with status ' + status))
}

Connection.prototype._onend = function () {
  this.push(null)
}

Connection.prototype._resolveAndConnect = function (port, host) {
  const self = this
  dns.lookup(host, function (err, ip) {
    if (err) return self.destroy(err)
    if (!ip) return self.destroy(new Error('Could not resolve ' + host))
    self._connect(port, ip)
  })
}

Connection.prototype._connect = function (port, ip) {
  if (this.destroyed) return
  binding.utp_napi_connect(this._utp._handle, this._handle, port, ip)
}

Connection.prototype._onconnect = function () {
  this._connected = true
  if (this._writing) {
    const cb = this._callback
    const data = this._writing[0]
    this._callback = null
    this._writing = null
    this._write(data, null, cb)
  }
  this.emit('connect')
}

Connection.prototype.destroy = function (err) {
  if (this.destroyed) return
  this.destroyed = true
  if (err) this._error = err
  binding.utp_napi_connection_close(this._handle)
}
