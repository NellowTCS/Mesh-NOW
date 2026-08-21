export default {
  title: "Mesh-NOW",
  url: "https://NellowTCS.github.io/Mesh-NOW",
  logo: { alt: "Mesh-NOW", href: "./" },
  favicon: "",
  theme: {
    name: "ruby",
    defaultMode: "system",
    enableModeToggle: true,
    positionMode: "top",
    codeHighlight: true,
    customCss: [],
    copyWidgets: {
      enabled: true,
      raw: true,
      context: true,
    },
  },
  layout: {
    footer: {
      style: "complete",
      description: "Lightweight mesh networking protocol for ESP32 using ESP-NOW.",
      branding: true,
      columns: [
        {
          title: "Resources",
          links: [
            { text: "Getting Started", url: "./getting-started/quickstart" },
            { text: "Protocol Guide", url: "/guide/message-format" },
            { text: "API Reference", url: "/api/" },
          ],
        },
        {
          title: "Community",
          links: [
            { text: "GitHub", url: "https://github.com/NellowTCS/Mesh-NOW" },
            { text: "Issues", url: "https://github.com/NellowTCS/Mesh-NOW/issues" },
            { text: "Discussions", url: "https://github.com/NellowTCS/Mesh-NOW/discussions" },
          ],
        },
      ],
    },
  },
  plugins: {
    search: {
      semantic: true,
      showConfidence: true,
    },
    seo: {
      defaultDescription:
        "Mesh-NOW is a lightweight mesh networking protocol library for ESP32 microcontrollers. Device-to-device communication over ESP-NOW with automatic peer discovery, message routing, and encryption.",
      openGraph: { defaultImage: "" },
      twitter: { cardType: "summary_large_image" },
    },
    sitemap: {
      defaultChangefreq: "weekly",
      defaultPriority: 0.8,
    },
    mermaid: {},
    git: {},
    llms: {
      fullContext: true,
    },
  },
  search: true,
  minify: true,
  autoTitleFromH1: true,
  copyCode: true,
  pageNavigation: true,
  navigation: [
    { title: "Home", path: "/", icon: "home" },
    {
      title: "Getting Started",
      icon: "rocket",
      collapsible: false,
      children: [
        { title: "Quick Start", path: "/getting-started/quickstart", icon: "play" },
        { title: "Installation", path: "/getting-started/installation", icon: "download" },
        { title: "Core Concepts", path: "/getting-started/concepts", icon: "book" },
      ],
    },
    {
      title: "Guide",
      icon: "book-open",
      collapsible: false,
      children: [
        { title: "Message Format", path: "/guide/message-format", icon: "file-text" },
        { title: "Peer Discovery", path: "/guide/peer-discovery", icon: "search" },
        { title: "Routing", path: "/guide/routing", icon: "radio" },
        { title: "Groups", path: "/guide/groups", icon: "users" },
        { title: "Encryption", path: "/guide/encryption", icon: "lock" },
        { title: "Reliability", path: "/guide/reliability", icon: "refresh-cw" },
        { title: "Configuration", path: "/guide/configuration", icon: "settings" },
      ],
    },
    {
      title: "API Reference",
      icon: "code",
      collapsible: false,
      children: [
        { title: "Mesh NOW API", path: "/api/", icon: "box" },
        { title: "Message Queue", path: "/api/message-queue", icon: "layers" },
      ],
    },
    {
      title: "Protocol",
      icon: "file-code",
      collapsible: false,
      children: [
        { title: "Wire Format", path: "/protocol/wire-format", icon: "hash" },
        { title: "State Machine", path: "/protocol/state-machine", icon: "cpu" },
      ],
    },
    {
      title: "GitHub",
      path: "https://github.com/NellowTCS/Mesh-NOW",
      icon: "github",
      external: true,
    },
  ],
  footer: "Built with [docmd](https://docmd.io). [View on GitHub](https://github.com/NellowTCS/Mesh-NOW).",
  editLink: {
    enabled: true,
    baseUrl: "https://github.com/NellowTCS/Mesh-NOW/edit/main/Docs/docs",
    text: "Edit this page",
  },
};
