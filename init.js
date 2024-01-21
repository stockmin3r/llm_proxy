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
	mainWindow.loadURL("http://localhost:8086/index.html");
	mainWindow.webContents.on("did-fail-load", function() {
		mainWindow.loadURL("http://localhost:8086/index.html");
	});
}

app.on('certificate-error', (event, webContents, url, error, certificate, callback) => {
  event.preventDefault();
  callback(true);
});

app.whenReady().then(() => {
	if (process.platform === "linux") {
		var path = require.main.path.split("/").slice(0,-1).join("/");
		path = "/panda";
		spawn(path + "/bin/llm_proxy", []);
	} else if (process.platform === "win32") {

	}
	createWindow();
});

app.on('activate', () => {
	if (BrowserWindow.getAllWindows().length == 0)
		createWindow();
});
