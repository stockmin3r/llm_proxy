const { app, BrowserWindow } = require('electron');
const { spawn }              = require('child_process');
const os                     = require("os")

const homedir = os.homedir();

app.disableHardwareAcceleration();

function createWindow() {
	const mainWindow = new BrowserWindow({
		width:1280,
		height:720,
		webPreferences: {
			nodeIntegration: false,
			contextIsolation: false
		}
	})
	mainWindow.loadURL("http://localhost:8485");
	mainWindow.webContents.on("did-fail-load", function() {
		mainWindow.loadURL("http://localhost:8485");
	});
}

app.on('certificate-error', (event, webContents, url, error, certificate, callback) => {
  event.preventDefault();
  callback(true);
});

app.whenReady().then(() => {
	spawn("c:\\ai\LLM-Proxy\LLM-Proxy.exe", [])
	createWindow();
});

app.on('activate', () => {
	if (BrowserWindow.getAllWindows().length == 0)
		createWindow();
});
