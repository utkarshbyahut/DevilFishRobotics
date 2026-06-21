// Event listeners for connect/disconnect buttons
document.getElementById('connectSerial').addEventListener('click', () => {
  initSerial();
});
document.getElementById('disconnectSerial').addEventListener('click', disconnectSerial);


// Event listeners for request buttons
document.getElementById('statusRequest').addEventListener('click', () => {
  writeToStream('S');
});
document.getElementById("setNameRequest").addEventListener('click', () => {
  if ( document.getElementById("newName").value == ""){
    alert("Name can not be empty")
  }
  else{
    writeToStream('N ' + document.getElementById("newName").value);
  }
});
document.getElementById('calibrateRequest').addEventListener('click', () => {
  writeToStream('C');
});
document.getElementById('timeRequest').addEventListener('click', () => {
  const epochTimeInSeconds = Math.floor(Date.now() / 1000);
  writeToStream('T ' + epochTimeInSeconds);
});
document.getElementById('upRequest').addEventListener('click', () => {
  writeToStream('M 100');
});
document.getElementById('neutralRequest').addEventListener('click', () => {
  writeToStream('M 50');
});
document.getElementById('downRequest').addEventListener('click', () => {
  writeToStream('M 0');
});
document.getElementById('profileRequest').addEventListener('click', () => {
  writeToStream('P');
});
document.getElementById('dataRequest').addEventListener('click', () => {
  writeToStream('D');
});
document.getElementById("manualRequest").addEventListener('click', () => {
  writeToStream('M ' + document.getElementById("manualNumber").value);
});
document.getElementById("manualNumber").addEventListener("keyup", async function (event) {
  if (event.keyCode === 13) {
    writeToStream('M ' + document.getElementById("manualNumber").value);
  }
});
document.getElementById('clearDataRequest').addEventListener('click', () => {
  if (confirm("Are you Sure")){writeToStream('R');}
});


// Event listeners for graph buttons
document.getElementById('ClearGraph').addEventListener('click', () => {
  if (confirm("Are you Sure")){
    trace1.x = [];
    trace1.y = [];
    Plotly.newPlot('dataGraph', data, layout);
  }
});
document.getElementById('ClearAll').addEventListener('click', () => {
  if (confirm("Are you Sure")){
    trace1.x = [];
    trace1.y = [];
    Plotly.newPlot('dataGraph', data, layout);
    writeToStream('R');
  }
});
document.getElementById("DownloadCSV").addEventListener('click', function() {
  downloadCSV([trace1.x, trace1.y], "data.csv");
});



// Event listeners for manual serial input terminal
document.getElementById('sendSerial').addEventListener('click', () => {
  sendSerialLine();
});
document.getElementById("inputSerial").addEventListener("keyup", async function (event) {
    if (event.keyCode === 13) {
        sendSerialLine();
    } else if (event.keyCode === 38) { // Key up
      scrollHistory(1);
    } else if (event.keyCode === 40) { // Key down
      scrollHistory(-1);
  }
});
document.getElementById("ClearTerminal").addEventListener('click', () => {
    document.getElementById('terminal').value = "";
});
document.getElementById("downloadTerminal").addEventListener('click', function() {
  downloadTextFile("terminal", "terminal.txt");
});

