# utp-napi

utp-native backed by n-api

```sh
npm install utp-napi
```

## Usage

```js
const utp = require('utp-napi')

const sock = utp()

sock.on('message', function (buf, rinfo) {
  console.log(buf, rinfo)
})

sock.bind(8000, function () {
  sock.send(Buffer.from('Hello World!'), 0, 12, 8000, '127.0.0.1')
})
```

## License

MIT
