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

/*
 * Recieve answers from llm_proxy.c and send them to the channel the question was asked in (or DM)
 */
panda_proxy_socket.on('data', function(data) {
	var question = question_list.find(x => x.id == data.toString().split(" ")[0]);
	var message  = question.message;

	var token = data.toString();
	token     = token.substr(token.indexOf(" ")+1);
	if (token == "\n") {
		question.newline = "\n";
		question.tokens += "\n";
		return;
	}

	if (token == "") {
		question.tokens += " ";
		return;
	}
	question.tokens += token;
	if (question.answer) {
		if (question.tokenbuf && question.tokenbuf.length + token.length < 64) {
			question.tokenbuf += token;
			question.newline = "";
			return;
		}
		if (question.tokenbuf && (question.tokenbuf.length + question.tokens.length > 2000)) {
			console.log("token buff too large");
			question.sent_answer = true;
			question.channel.send(question.newline + token).then((sentMessage) => {
				question.answer = sentMessage;
				return;
			});
		}
		question.answer.edit({content:question.tokenbuf+question.tokens});
		question.tokenbuf = "";
	} else {
		if (question.sent_answer) {
			question.tokenbuf += token;
			question.newline = "";
			return;
		}
		question.sent_answer = true;
		question.channel.send(question.newline + token).then((sentMessage) => {
			question.answer = sentMessage;
		});
	}
	question.newline = "";
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

function panda_ask_question(message, channel)
{
	var question = {message:message, id:QID(), edit:false, channel: channel, tokens:""};
	question_list.push(question);
	panda_proxy_socket.write(question.id + " " + question.message + "\n");
}

/*
 * Message Handler
 */
bot.on('messageCreate', message => {
	var channel;

	if (message.author.bot)
		return;

	if (message.guild.channels)
		channel = message.guild.channels.cache.find(channel => channel.name === "general");

	if (message.content.startsWith('panda:'))
		panda_ask_question(message.content.substr(6), channel);
});
