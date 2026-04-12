// Import the express module

const fs = require('fs');
const path = require('path');
const express = require('express');
const morgan = require('morgan');

// Create an instance of express
const app = express();


// Define a simple route
app.get('/', (req, res) => {
    res.send('Hello, world!');
});


// Logging middleware
app.use(morgan('combined'));

// Middleware to parse JSON bodies
app.use(express.json());

// Function to handle POST /devices

function handlePostDevices(req, res) {
    // Expect incoming shape: { data: [[mac, rssi, name], ...] }
    const { data } = req.body;

    if (!Array.isArray(data)) {
        return res.status(400).json({ error: 'Request must include `data` array' });
    }

    // Convert array-of-arrays to objects
    const entries = [];
    for (const item of data) {
        if (!Array.isArray(item) || item.length < 1) continue;
        const mac = item[0];
        const rssi = item.length > 1 ? Number(item[1]) : null;
        const name = item.length > 2 ? item[2] : null;
        entries.push({ mac, rssi, name });
    }

    // Get current date and time for filename
    const now = new Date();
    const pad = (n) => n.toString().padStart(2, '0');
    const yyyymmdd = now.getFullYear().toString() + pad(now.getMonth() + 1) + pad(now.getDate());
    const hhmmss = pad(now.getHours()) + '_' + pad(now.getMinutes()) + '_' + pad(now.getSeconds());
    const filename = `macs_${yyyymmdd}_${hhmmss}.txt`;
    const outputDir = path.join(__dirname, 'output');
    const filePath = path.join(outputDir, filename);

    // Prepare file content: ISO time + each entry as a JSON line
    const isoTime = now.toISOString();
    const jsonLines = entries.map((e) => JSON.stringify(e));
    const fileContent = [isoTime, ...jsonLines].join('\n');

    // Ensure output directory exists, then write file
    fs.mkdir(outputDir, { recursive: true }, (dirErr) => {
        if (dirErr) {
            console.error('Error creating output directory:', dirErr);
            return res.status(500).json({ error: 'Failed to create output directory' });
        }
        fs.writeFile(filePath, fileContent, (err) => {
            if (err) {
                console.error('Error writing MACs file:', err);
                return res.status(500).json({ error: 'Failed to save MACs' });
            }
            res.status(201).json({ message: 'Entries received and saved', filename, count: entries.length });
        });
    });
}

// Route for POST /store
app.post('/store', handlePostDevices);

// Start the server on port 3000
const PORT = 9999;
app.listen(PORT, () => {
    console.log(`Server is running on port ${PORT}`);
});
