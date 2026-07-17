import { defineUserConfig } from "vuepress";
import { defaultTheme } from "@vuepress/theme-default";
import { viteBundler } from "@vuepress/bundler-vite";
import { markdownMathPlugin } from "@vuepress/plugin-markdown-math";

export default defineUserConfig({
  lang: "en-US",

  title: "nekosu",
  description: "Nekosu is a kernel level rootkit for files manager access control.",

  base: "/", // Cloudflare Pages 根路径部署

  theme: defaultTheme({
    navbar: [
      {
        text: "Home",
        link: "/",
      },
    ],
  }),

  plugins: [
    markdownMathPlugin({
      // 默认使用 KaTeX
    }),
  ],

  bundler: viteBundler(),
});
