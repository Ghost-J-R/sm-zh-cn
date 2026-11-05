> 这个 fork 是对超级银河战士 agugu 汉化 ROM 的适配。本项目没有 ROM 文件，你需要自己准备汉化后的 ROM；目前没有适配繁体中文。

# sm

Our discord server is: https://discord.gg/AJJbJAzNNJ

Early version. It has bugs and the code is messy.

For building instructions, see: https://github.com/snesrev/sm/blob/main/BUILDING.md

Put sm.smc (sha1 hash da957f0d63d14cb441d215462904c4fa8519c613) in the root folder. When running, it will run both versions and compare frame by frame. If it detects a mismatch, it saves a snapshot in saves/ and displays a counter on screen counting down from 300.
