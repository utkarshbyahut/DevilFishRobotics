var trace1 = { x: [], y: [], type: 'scatter'};
  
var layout = {
    title: {
      text:'Depth Data',
      font: {family: 'Arial, Sans-serif', size: 24},
      xref: 'paper',
      x: 0.5,
    },
    xaxis: {
      title: {
        text: 'Time',
        font: {family: 'Arial, Sans-serif', size: 18, color: '#7f7f7f'}
      },
    },
    yaxis: {
      title: {
        text: 'Depth (m)',
        font: {family: 'Arial, Sans-serif', size: 18, color: '#7f7f7f'}
      }
    }
  };
  
  
  var data = [trace1];
  
  Plotly.newPlot('dataGraph', data, layout);