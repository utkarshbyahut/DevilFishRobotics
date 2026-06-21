function downloadCSV(arrayOfArrays, filename) {
    // Convert Date objects to ISO strings
    const formatted = arrayOfArrays.map(row => row.map(item => item instanceof Date ? item.toLocaleTimeString() : item));
  
    // Transpose the array
    const transposed = formatted[0].map((_, i) => formatted.map(row => row[i]));
  
    // Add column headers
    const headers = ['Time', 'Depth']; // replace with your headers
    transposed.unshift(headers);
  
    // Convert arrays to CSV string
    const csvContent = "data:text/csv;charset=utf-8," 
      + transposed.map(e => e.join(",")).join("\n");
  
    // Create a download link and click it to start the download
    const encodedUri = encodeURI(csvContent);
    const link = document.createElement("a");
    link.setAttribute("href", encodedUri);
    link.setAttribute("download", filename);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  }
  
function downloadTextFile(textAreaId, filename) {
    // Get the text from the text area
    const text = document.getElementById(textAreaId).value;
  
    // Create a Blob with the text
    const textBlob = new Blob([text], { type: 'text/plain' });
  
    // Create a URL for the Blob
    const url = URL.createObjectURL(textBlob);
  
    // Create a download link and click it to start the download
    const link = document.createElement("a");
    link.href = url;
    link.download = filename;
    link.click();
  
    // Revoke the URL to free up memory
    URL.revokeObjectURL(url);
  }
  
  document.getElementById("newName").value = (localStorage.teamName == undefined ? "" : localStorage.teamName);