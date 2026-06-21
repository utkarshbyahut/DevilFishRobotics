let port;
let reader;
let inputDone;
let outputStream;
let writableStreamClosed;
let historyIndex = -1;

const lineHistory = [];


async function initSerial() {
    if ('serial' in navigator) {
      try {
        const requestOptions = { filters: [{ usbVendorId: 0x239a }] }; // Example filter
        //port = await navigator.serial.requestPort(requestOptions);
        port = await navigator.serial.requestPort();
        await port.open({ baudRate: 115200 });

        // Create streams
        const textEncoder = new TextEncoderStream();
        writableStreamClosed = textEncoder.readable.pipeTo(port.writable);
        outputStream = textEncoder.writable.getWriter();

        const textDecoder = new TextDecoderStream();
        inputDone = port.readable.pipeTo(textDecoder.writable);
        reader = textDecoder.readable.getReader();

        // Update UI
        document.getElementById('connectSerial').classList.add('hidden');
        document.getElementById('disconnectSerial').classList.remove('hidden');
        document.getElementById('statusIndicator').classList.add('status-green');
        document.getElementById('statusIndicator').classList.remove('status-red');

        // Start reading
        readLoop();
      } catch (e) {
        alert("Serial Connection Failed");
        console.error('There was an error opening the serial port:', e);
      }
    } else {
      console.error('Web Serial API not supported.');
    }
  }

async function disconnectSerial() {
    if (reader) {
        try {
          await reader.cancel();
          await inputDone.catch(() => {});
        } catch (e) {
          console.error('There was an error canceling the reader:', e);
        }
        reader = null;
        inputDone = null;
      }
      if (outputStream) {
        try {
          await outputStream.close();
          await writableStreamClosed.catch(() => {});
        } catch (e) {
          console.error('There was an error closing the outputStream:', e);
        }
        outputStream = null;
        writableStreamClosed = null;
      }
      if (port) {
        try {
          await port.close();
        } catch (e) {
          console.error('There was an error closing the port:', e);
        }
        port = null;
      }
    
      // Update UI
      document.getElementById('connectSerial').classList.remove('hidden');
      document.getElementById('disconnectSerial').classList.add('hidden');
      document.getElementById('statusIndicator').classList.add('status-red');
      document.getElementById('statusIndicator').classList.remove('status-green');
}

async function readLoop() {
    var buffer = "";
    while (true) {
        try {
            const { value, done } = await reader.read();
            if (done) {
                // Allow the serial port to be closed later.
                reader.releaseLock();
                break;
            }
            if (value) {
                //document.getElementById('terminal').value += value;
                buffer += value;
            }

            if (buffer.includes('\n')){
                const msgLine = buffer.substring(0,buffer.indexOf('\n')+1);
                buffer = buffer.substring(buffer.indexOf('\n')+1);
                try {
                const msg = JSON.parse(msgLine);
                switch(msg.type) {
                    case 'Status':
                    statusHandler(msg);
                    break;
                    case 'data':
                    dataHandler(msg);
                    break;
                }
                console.log(JSON.stringify(msg));

                document.getElementById('terminal').value += msgLine;

                } catch (e) {
                console.log(e);
                document.getElementById('terminal').value += msgLine;
                }
                document.getElementById('terminal').scrollTop =  document.getElementById('terminal').scrollHeight;
            }
        } catch (e) {
            console.error('There was an error reading from the serial port:', e);
            await disconnectSerial();
            alert("Lost Serial Connection")
            break;
          }
    }
  }

async function writeToStream(...lines) {
    for (const line of lines) {
      document.getElementById('terminal').value += ">   ";
      await outputStream.write(line + '\n');
    }
  }


function statusHandler(msg){
    document.getElementById("teamStatus").innerHTML = msg.team;
    document.getElementById("depthStatus").innerHTML = msg.depth;
    document.getElementById("pressureStatus").innerHTML = msg.pressure;
    document.getElementById("temperatureStatus").innerHTML = msg.temperature;
    document.getElementById("batteryStatus").innerHTML = msg.battery;
    //document.getElementById("positionStatus").innerHTML = msg.position;
    document.getElementById("timeStatus").innerHTML = new Date(msg.time * 1000).toLocaleString();
    if(msg.calibration){
      document.getElementById('calibrateRequest').classList.add('status-green');
      document.getElementById('calibrateRequest').classList.remove('status-red');
    }
    else{
      document.getElementById('calibrateRequest').classList.add('status-red');
      document.getElementById('calibrateRequest').classList.remove('status-green');
    }
    if(msg.timeSet){
      document.getElementById('timeRequest').classList.add('status-green');
      document.getElementById('timeRequest').classList.remove('bg-blue-500');
    }
    else{
      document.getElementById('timeRequest').classList.add('bg-blue-500');
      document.getElementById('timeRequest').classList.remove('status-green');
    }
  }


function dataHandler(msg){
    if (msg.reset !== undefined && msg.reset){
      trace1.x = [];
      trace1.y = [];
      Plotly.newPlot('dataGraph', data, layout);
    }
    Plotly.extendTraces('dataGraph',{
      x: [[new Date(msg.time * 1000)]],
      y: [[msg.depth_m]]}, [0]);
  }



function scrollHistory(direction) {
  // Clamp the value between -1 and history length
  historyIndex = Math.max(Math.min(historyIndex + direction, lineHistory.length - 1), -1);
  if (historyIndex >= 0) {
      document.getElementById("inputSerial").value = lineHistory[historyIndex];
  } else {
      document.getElementById("inputSerial").value = "";
  }
}

function sendSerialLine() {
  const input = document.getElementById('inputSerial').value;
  lineHistory.unshift(input);
  writeToStream(input);
  document.getElementById("inputSerial").value = "";
  document.getElementById("inputSerial").value = "";
}