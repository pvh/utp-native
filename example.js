const udp = require('./')
const fs = require('fs')

const sock = udp()
const speed = require('speedometer')()
const p = require('prettier-bytes')

var read = 0

const listening = process.argv.indexOf('-l') > -1
const connecting = process.argv.indexOf('-c') > -1
const i = process.argv.indexOf('-p')
const ps = i === -1 ? 0 : Number(process.argv[i + 1])

sock.on('error', console.error)

sock.on('close', function () {
  console.log('socket fully closed')
})

sock.on('connection', function (connection) {
  connection.on('error', console.error)
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

if (listening) sock.listen(8080, 'localhost')
if (listening && connecting) sock.on('listening', connect)
else if (connecting) connect()

function connect () {
  const sock = udp()
  var total = 0
  var tick = 0
  var packs = 0
  const c = sock.connect(8080)
  c.on('error', console.error)
  c.setPacketSize(ps)
  const speed = require('speedometer')()
  c.on('data', function (data) {
    packs++
    total += data.length
    speed(data.length)
  })
  c.on('connect', function () { 
    console.log('connected')
  })
  c.write('hi\n')

  setInterval(function () {
    console.log(p(total) + ' ' + p(speed()) + ' ' + packs + ' ' + (++tick))
  }, 1000)

  return
  sock.send(Buffer.from('hello'), 0, 5, port, '127.0.0.1', function loop () {
    sock.send(Buffer.from('worlds'), 0, 6, port, 'localhost', function () {
      console.log('all sends flushed')
      sock.close()
    })
  })
}
