const binding = require('./binding')
const stream = require('stream')
const util = require('util')

const EMPTY = Buffer.alloc(0)

module.exports = Connection

function Connection (utp, port, address) {
  stream.Duplex.call(this)

  this.remoteAddress = address
  this.remoteFamily = 'IPv4'
  this.remotePort = port

  this._utp = utp
  this._handle = Buffer.alloc(binding.sizeof_utp_napi_connection_t)
  this._buffer = Buffer.allocUnsafe(65536 * 2)
  this._offset = 0
  this._view = new Uint32Array(this._handle.buffer, 0, 2)

  binding.utp_napi_connection_init(this._handle, this, this._buffer,
    this._onread
  )

  // set min packet size
  // this._view[0] = 65536
}

util.inherits(Connection, stream.Duplex)

Connection.prototype.address = function () {
  return this._utp.address()
}

Connection.prototype._read = function () {
  // TODO: backpressure
}

Connection.prototype._write = function (data, enc, cb) {
  
}

Connection.prototype._onread = function (size) {
  const buf = this._buffer.slice(this._offset, this._offset += size)

  this.push(buf)

  if (this._buffer.length - this._offset <= 65536) {
    this._buffer = Buffer.allocUnsafe(this._buffer.length)
    this._offset = 0
    return this._buffer
  }

  return EMPTY
}
