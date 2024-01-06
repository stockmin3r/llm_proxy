require('dotenv').config();

const Discord = require('discord.js');
const { TFAutoModelForQuestionAnswering, AutoTokenizer } = require('node-transformers');
const fs    = require('fs');
const net   = require('net');
const ini   = require('ini');
const TOKEN = process.env.TOKEN;

class PandaBot {
  constructor() {
    this.client = new Discord.Client({
      intents: [
        Discord.GatewayIntentBits.Guilds,
        Discord.GatewayIntentBits.GuildMessages,
        Discord.GatewayIntentBits.DirectMessages,
        Discord.GatewayIntentBits.MessageContent,
      ]
    });

    // Load configurations from config.ini
    this.config          = ini.parse(fs.readFileSync('./config.ini', 'utf-8'));
    this.allowedChannels = this.config.channels.split(',');
    this.pandaPort       = parseInt(this.config.server.panda_port);
    this.currentModel    = this.config.models.model;

    // Bind event handlers
    this.client.on('ready', this.onReady.bind(this));
    this.client.on('messageCreate', this.onMessageCreate.bind(this));

    // Log in to Discord with the bot token
    this.client.login(TOKEN);
  }

  async switchModel(newModel) {
    try {
      console.log(`Switching to model: ${newModel}`);
      this.currentModel = newModel;

      // Destroy the current client
      await this.client.destroy();

      // Create a new client with the new model
      this.client = new Discord.Client({
        intents: [
          Discord.GatewayIntentBits.Guilds,
          Discord.GatewayIntentBits.GuildMessages,
          Discord.GatewayIntentBits.DirectMessages,
          Discord.GatewayIntentBits.MessageContent,
        ]
      });

      // Bind event handlers to the new client
      this.client.on('ready',         this.onReady.bind(this));
      this.client.on('messageCreate', this.onMessageCreate.bind(this));

      // Log in to Discord with the new bot token
      await this.client.login(TOKEN);

      console.log('Model switched successfully.');
    } catch (error) {
      console.error(`Error switching to model: ${error.message}`);
      throw new Error(`Error switching to model: ${error.message}`);
    }
  }

  async answerQuestion(question, context) {
    try {
      // Load pre-trained model and tokenizer
      const model = await TFAutoModelForQuestionAnswering.fromPretrained(this.currentModel);
      const tokenizer = await AutoTokenizer.fromPretrained(this.currentModel);

      // Tokenize the input
      const inputs = await tokenizer(question, context, { return_tensors: 'tf' });

      // Get the answer
      const outputs = await model.predict(inputs);
      const answer = tokenizer.convertFromRawResponse(outputs);

      return answer;
    } catch (error) {
      throw new Error(`Error answering the question: ${error.message}`);
    }
  }

  onReady() {
    // Log server information
    console.log(`Number of models: 1`);
    console.log(`Port: ${this.pandaPort}`);
    console.log(`Current model: ${this.currentModel}`);
    console.log(`Allowed channels: ${this.allowedChannels.join(', ')}`);
  }

  async onMessageCreate(msg) {
    try {
      // Check if the message is from an allowed channel
      if (!this.allowedChannels.includes(msg.channel.name)) {
        return;
      }

      // Check if the message contains the specified command
      if (msg.content.startsWith('panda:')) {
        const [_, question, context] = msg.content.split(' ');
        const answer = await this.answerQuestion(question, context);
        msg.reply(`Answer: ${answer}`);
      } else if (msg.content.startsWith('$SWITCH_MODEL')) {
        const newModel = msg.content.split(' ')[1];
        await this.switchModel(newModel);
        msg.reply(`Switched to model: ${newModel}`);
      }
    } catch (error) {
      console.error(error.message);
      msg.reply(`Error processing your request: ${error.message}`);
    }
  }
}

const pandaBot = new PandaBot();
