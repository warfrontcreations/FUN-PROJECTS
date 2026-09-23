module.exports = {
  apps: [
    {
      name: 'whatsapp-assistant',
      script: 'index.js',
      autorestart: true,
      restart_delay: 3000,
      max_restarts: 20,
      watch: false,
    },
  ],
};
