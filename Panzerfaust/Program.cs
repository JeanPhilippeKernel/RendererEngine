using System;

using Avalonia;
using Avalonia.Controls.Primitives;

namespace Panzerfaust;

class Program
{
    // Initialization code. Don't use any Avalonia, third-party APIs or any
    // SynchronizationContext-reliant code before AppMain is called: things aren't initialized
    // yet and stuff might break.
    [STAThread]
    public static void Main(string[] args) => BuildAvaloniaApp()
        .StartWithClassicDesktopLifetime(args);

    // Avalonia configuration, don't remove; also used by visual designer.
    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .WithInterFont()
            .LogToTrace();

}


//// See https://aka.ms/new-console-template for more information
//using System.Diagnostics;

//string enginePath =  string.Empty;
//string configuration = string.Empty;
//string workingDirectory = string.Empty;
//string exampleProjectConfig = string.Empty;

//const string editorAppName = "zEngineEditor";
//var engineArgs = new List<string> { "--projectConfigFile" };

//#if DEBUG
//configuration = "Debug";
//exampleProjectConfig = @$"{Environment.CurrentDirectory}\..\..\..\Examples\projectConfig.json";
//engineArgs.Add(exampleProjectConfig);
//#else
//configuration = "Release";
//#endif

//enginePath = @$"{Environment.CurrentDirectory}\Editor\{editorAppName}.exe";
//workingDirectory = @$"{Environment.CurrentDirectory}\Editor";

//ProcessStartInfo processStartInfo = new ProcessStartInfo(enginePath, engineArgs)
//{
//    UseShellExecute = false,
//    WorkingDirectory = workingDirectory
//};

//var engineProcess = Process.Start(processStartInfo);
//Console.WriteLine("Engine is running..");

//engineProcess.WaitForExit();

//if (engineProcess.ExitCode == -2)
//{
//    Console.WriteLine("Failed to start the engine, invalid  args");
//}


