const udp = require('./')

const sock = udp()
const speed = require('speedometer')()
const p = require('prettier-bytes')
var read = 0

sock.on('close', function () {
  console.log('socket fully closed')
})

sock.on('connection', function (connection) {
  console.log('new connection')
  connection.on('data', function (data) {
    console.log('ondata', data.length, p(speed(data.length)))
  })
})

sock.on('message', function (buf, rinfo) {
  read += buf.length
  console.log(buf, read, rinfo)
})

sock.listen(8080, 'localhost')
sock.on('listening', function () {
  const { port } = sock.address()

  console.log('listening', port)

  return
  sock.send(Buffer.from('hello'), 0, 5, port, '127.0.0.1', function loop () {
    sock.send(Buffer.from('worlds'), 0, 6, port, 'localhost', function () {
      console.log('all sends flushed')
      sock.close()
    })
  })
})
