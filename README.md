# imgui-vgmplayer v0.44
This is a VGMplayer based on imgui, focused on the Windows platform. It currently supports VGM/VGZ playback for YM2612 and YM2151 microcontrollers and features advanced visualization capabilities. Notably, this program was entirely written in Claude 4.5 Opus; I was only responsible for designing the prompts and debugging the functionality  
All the hints are here, which is also the changelog: [https://github.com/denjhang/imgui-vgmplayer/blob/main/imgui-vgmplayer%E6%97%A5%E5%BF%97.txt  ](https://github.com/denjhang/imgui-vgmplayer-v0.44/blob/main/imgui-vgmplayer%E6%97%A5%E5%BF%97.txt)  
# Note
The main program is located at: https://github.com/denjhang/imgui-vgmplayer-v0.44/tree/main/examples/example_vgm_player.  
This player is still under development and has many imperfections.  
1. To implement piano visualization and registers, I used a set of shadow registers, which is almost a simplified VGM chip simulator.  
2. To implement music playback, I compiled the complete libvgm as a library.  
3. To implement oscilloscope functionality for each channel, I also compiled a modified version of libvgm for modifiers.  
Therefore, I'm essentially using three libvgm libraries, which makes the program somewhat cluttered. However, considering that this is entirely written by AI, as long as these details are understood, AI can easily code, add new features, and fix bugs.  
