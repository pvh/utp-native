const udp = require('./')
const fs = require('fs')

const sock = udp()
const speed = require('speedometer')()
const p = require('prettier-bytes')
var read = 0

sock.on('close', function () {
  console.log('socket fully closed')
})

sock.on('connection', function (connection) {
  console.log('(new connection)')
  const data = Buffer.alloc(655360)
  fs.createReadStream('/dev/zero').pipe(connection)
  connection.on('data', function (data) {
    // console.log('ondata', data.length, p(speed(data.length)))
  })
  connection.on('end', function () {
    console.log('onend')
  })
  connection.on('close', function () {
    console.log('fully closed')
  })
})

sock.on('message', function (buf, rinfo) {
  read += buf.length
  console.log(buf, read, rinfo)
})

sock.listen(8080, 'localhost')
if (process.argv.indexOf('--connect') > -1) sock.on('listening', connect)

function connect () {
  const sock = udp()
  const c = sock.connect(8080)
  c.setPacketSize(65536)
  const speed = require('speedometer')()
  c.on('data', function (data) {
    speed(data.length)
  })
  c.on('connect', function () { 
    console.log('connected')
  })
  c.write('hi')

  setInterval(function () {
    console.log(p(speed()))
  }, 1000)

  return
  sock.send(Buffer.from('hello'), 0, 5, port, '127.0.0.1', function loop () {
    sock.send(Buffer.from('worlds'), 0, 6, port, 'localhost', function () {
      console.log('all sends flushed')
      sock.close()
    })
  })
}
