const udp = require('./')

const sock = udp()
var read = 0

sock.on('close', function () {
  console.log('socket fully closed')
})

sock.on('message', function (buf, rinfo) {
  read += buf.length
  console.log(buf, read, rinfo)
})

sock.bind(0, 'localhost')
sock.on('listening', function () {
  const { port } = sock.address()
  sock.send(Buffer.from('hello'), 0, 5, port, '127.0.0.1', function loop () {
    sock.send(Buffer.from('worlds'), 0, 6, port, 'localhost', function () {
      console.log('all sends flushed')
      sock.unref()
    })
  })
})
