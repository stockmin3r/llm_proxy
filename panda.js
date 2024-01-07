/*
 * Written by Aryaman Pattnayak & stockmin3r
 *
 * - https://github.com/DarryCrucian
 * - https://github.com/stockmin3r
 */

require('dotenv').config();
const {Client,GatewayIntentBits,Discord,Partials} = require('discord.js');
const bot          = new Client({
intents: [
	GatewayIntentBits.Guilds,
	GatewayIntentBits.GuildMessages,
	GatewayIntentBits.DirectMessages,
	GatewayIntentBits.GuildBans,
	GatewayIntentBits.MessageContent,
],
partials: [
	Partials.Channel,
	Partials.Message
]});
const TOKEN           = process.env.TOKEN;
const fs              = require('fs');
const net             = require('net');
const ini             = require('ini');
const request         = require('request');

const config          = ini.parse(fs.readFileSync('./config.ini', 'utf-8'));
const allowedChannels = config.channels.channels.split(',');
const QID             = size => [...Array(16)].map(() => Math.floor(Math.random() * 16).toString(16)).join('')

var   question_list   = [];
var   main_channel;

var   panda_proxy_socket = net.createConnection({port:config.server.panda_port});
panda_proxy_socket.on('data', function(data) {
	message = question_list.find(x => x.id == data.toString().split(" ")[0]);
	console.log("msg: " + JSON.stringify(message));
//	main_channel = message.guild.channels.cache.find(channel => channel.name === "general");
	msg = data.toString().split(" ")[1];
	if (msg == "\n" || msg == "")
		return;
	main_channel.send(msg);
});

bot.login(TOKEN);
bot.on('ready', () => {
	console.info(`Logged in as ${bot.user.tag}!`);
//	bot.channels.cache.get("1171834979936911443").send("Panda is uncensored - Ask panda anything!");
	console.log(allowedChannels.length);
});

function extractUrlFromCommand(command) {
    const urlRegex = /\$\((CURL\((.*?)\))\)/;
    const match = command.match(urlRegex);
    return match ? match[2] : null;
}

async function fetchDataFromUrl(url) {
    const response = await fetch(url);
    if (!response.ok) {
        throw new Error('Network response was not ok');
    }
    return await response.text();
}

function convertHtmlTableToCsv(html) {
    const dom = new JSDOM(html);
    const table = dom.window.document.querySelector('table');
    if (!table) {
        throw new Error('No table found in HTML');
    }

    let csv = [];
    const rows = table.querySelectorAll('tr');

    rows.forEach(row => {
        let rowData = [];
        row.querySelectorAll('th, td').forEach(cell => {
            rowData.push(cell.textContent.replace(/(\r\n|\n|\r)/gm, "").trim());
        });
        csv.push(rowData.join(','));
    });

    return csv.join('\n');
}

function panda_ask_question(message)
{
	var question = {message:message, id:QID()};
	question_list.push(question);
	panda_proxy_socket.write(question.id + " " + question.message.content + "\n");
	console.log("panda_ask_question: " + question.id + " " + question.message.content);
}

/*
 * Message Handler
 */
bot.on('messageCreate', message => {
	main_channel = message.guild.channels.cache.find(channel => channel.name === "general");
	console.log("msg");
	if (message.author.bot)
		return;

	if (message.content.startsWith('panda:')) {
		panda_ask_question(message);
	}
});
