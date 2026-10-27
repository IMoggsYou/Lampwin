# bundle-icons

The app icons that come with the app live in `assets/app-icons` as 256 px PNGs, already
cut to the icon itself: that keeps the installer small (the original `.icns` files carry
every size up to 1024 px).

To add an icon, put its file (`.icns`, `.png`, ...) into `assets/app-icons` and run:

```powershell
dotnet run --project tools/bundle-icons -- assets/app-icons
```

Every file is converted with the settings app's own code (`IconImport`) and replaced by
`<same name>.png`. Name the file after the program: the app gives the icon to the program
its name names (`IconNames`), for example `Discord.png` or `Visual_Studio_Code.png`.
