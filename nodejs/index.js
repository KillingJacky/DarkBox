
const si = require('systeminformation')
const cobs = require('cobs')
const SerialPort = require('serialport')
const Delimiter = require('@serialport/parser-delimiter')
const port = new SerialPort('/dev/ttyS4', {
    baudRate: 115200,
    autoOpen: false
})

var intervalHandler
var mainIntervalHanders = []

port.on('open', async function() {
    console.log(`serial is opened`)
    if (intervalHandler) clearInterval(intervalHandler)
    main()
})

port.on('close', function() {
    console.log(`serial is closed`)

    setTimeout(reopen, 2000)

    for (let h of mainIntervalHanders) {
        clearInterval(h)
    }
    mainIntervalHanders = []
})

function reopen() {
    intervalHandler = setInterval(() => {
        try {
            port.open()
        } catch (e) {
            console.log(e)
        }
    }, 2000)
}

console.log('recomputer bridge ...')
reopen()

const parser = port.pipe(new Delimiter({ delimiter: [0x0] }))

parser.on('data', function(data) {
    let buffDec = cobs.decode(data)
    let buffStr = buffDec.toString()
    let msgJson = JSON.parse(buffStr)
    console.log(`serial data: `, msgJson)
})




async function sendFloat(pktType, value) {
    let data = parseInt(value * 1000)
    buff = Buffer.alloc(5)
    buff.writeUInt8(pktType, 0)
    buff.writeUInt32LE(data, 1)
    let buffEnc = cobs.encode(buff)
    let buffDel = Buffer.from([0])
    if (port.isOpen) {
        port.write(Buffer.concat([buffEnc, buffDel]))
    }
}

async function sendString(pktType, str) {
    let strLen = str.length
    buff = Buffer.alloc(1 + strLen)
    buff.writeUInt8(pktType, 0)
    buff.write(str, 1, strLen)
    let buffEnc = cobs.encode(buff)
    let buffDel = Buffer.from([0])
    if (port.isOpen) {
        port.write(Buffer.concat([buffEnc, buffDel]))
    }
}

async function readCpuTemp() {
    let {main, cores, max} = await si.cpuTemperature()
    if (main < 0) main = 0
    console.log(`cpuTemp: ${main}`)
    sendFloat(1, main)
    return main
}

async function readCpuLoad() {
    let {currentload, avgload} = await si.currentLoad()
    console.log(`cpuLoad: ${currentload}%`)
    sendFloat(2, currentload)
    return currentload
}

async function readRam() {
    let {used, total} = await si.mem()
    let ramUsed = 100 * used / total
    console.log(`ram: ${ramUsed}%`)
    sendFloat(3, ramUsed)
    return ramUsed
}

async function readIp() {
    let defaultIf = await si.networkInterfaceDefault()
    let ifs = await si.networkInterfaces()
    let ip = "--.--.--.--"
    for (let iface of ifs) {
        if (iface.ifaceName === defaultIf) {
            ip = iface.ip4
            break
        }
    }
    console.log(`ip: ${ip}`)
    sendString(4, ip)
    return ip
}

async function main() {
    await readCpuTemp()
    await readCpuLoad()
    await readRam()
    await readIp()
    mainIntervalHanders.push(setInterval(readCpuTemp, 5000));
    mainIntervalHanders.push(setInterval(readCpuLoad, 10000));
    mainIntervalHanders.push(setInterval(readRam, 30000));
    mainIntervalHanders.push(setInterval(readIp, 60000));
}



