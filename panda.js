/*
 * Written by Aryaman Pattnayak & stockmin3r0
 *
 * - https://github.com/DarryCrucian
 * - https://github.com/stockmin3r
 */

require('dotenv').config();
const Discord      = require('discord.js');
const bot          = new Discord.Client({ intents: [
  Discord.GatewayIntentBits.Guilds,
  Discord.GatewayIntentBits.GuildMessages,
  Discord.GatewayIntentBits.DirectMessages,
  Discord.GatewayIntentBits.GuildBans,
  Discord.GatewayIntentBits.MessageContent,
]});
const TOKEN        = process.env.TOKEN;
const fs           = require('fs');
const request      = require('request');

bot.login(TOKEN);
bot.on('ready', () => {
	console.info(`Logged in as ${bot.user.tag}!`);
//	bot.channels.cache.get("1171834979936911443").send("Panda is uncensored - Ask panda anything!");
	const config = ini.parse(fs.readFileSync('./config.ini', 'utf-8'));
	const allowedChannels = config.channels.split(',');
});

function extractUrlFromCommand(command) {
    const urlRegex = /\$\((DOWNLOAD_URL\((.*?)\))\)/;
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

/*
 * Message Handler
 */
bot.on('messageCreate', msg => {
	var argv = "";

	main_channel = msg.guild.channels.cache.find(channel => channel.name === "general");
	if (msg.author.bot)
		return;

	console.log(msg.content);
	argv = msg.content.slice().trim().split(/ +/g);
	if (msg.content.startsWith('panda:')) {
		panda_msg(msg.content);
	}
});
