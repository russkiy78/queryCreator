import { defineConfig } from 'vitepress'

export default defineConfig({
  lang: 'en-US',
  title: 'queryCreator',
  description: 'Lightweight SQL query builder for C++20',
  base: '/queryCreator/',
  lastUpdated: true,
  cleanUrls: true,

  head: [['link', { rel: 'icon', href: '/queryCreator/favicon.ico' }]],

  themeConfig: {
    logo: { light: '/queryCreator/logo/logo-small-white.png', dark: '/queryCreator/logo/logo-small-white.png' },

    search: {
      provider: 'local'
    },

    nav: [
      { text: 'Home', link: '/' },
      { text: 'Guide', link: '/getting-started' },
      {
        text: 'API Reference',
        items: [
          { text: 'QcSqlQuery', link: '/api/select-queries' },
          { text: 'QcSqlQueryElement', link: '/api/where-conditions' },
          { text: 'QcSqlQueryValue', link: '/api/select-functions' },
          { text: 'INSERT/UPDATE/DELETE', link: '/api/dml-builders' }
        ]
      },
      { text: 'Source Code', link: 'https://github.com/russkiy78/queryCreator' }
    ],

    sidebar: {
      '/': [
        {
          text: 'Introduction',
          items: [
            { text: 'Home', link: '/' },
            { text: 'Getting Started', link: '/getting-started' }
          ]
        },
        {
          text: 'Query Builder',
          items: [
            { text: 'SELECT Queries', link: '/api/select-queries' },
            { text: 'WHERE Conditions', link: '/api/where-conditions' },
            { text: 'SELECT Functions', link: '/api/select-functions' },
            { text: 'JSON Fields', link: '/api/json-fields' },
            { text: 'INSERT/UPDATE/DELETE', link: '/api/dml-builders' }
          ]
        },
        {
          text: 'Drivers & Connections',
          items: [
            { text: 'SQL Dialects', link: '/api/dialects' },
            { text: 'Database Connection', link: '/api/connections' },
            { text: 'QueryCreator Facade', link: '/api/query-creator' }
          ]
        }
      ]
    },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/russkiy78/queryCreator' }
    ],

    footer: {
      message: 'Released under the MIT License.',
      copyright: 'Copyright © 2025-2026 queryCreator contributors'
    }
  }
})
