using Avalonia.Data.Converters;
using System;
using System.Globalization;

namespace Panzerfaust.Converters
{
    public class BoolToWidthConverter : IValueConverter
    {
        public static readonly BoolToWidthConverter Instance = new();

        public double ExpandedWidth { get; set; } = 200;
        public double CollapsedWidth { get; set; } = 52;

        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
            => value is true ? ExpandedWidth : CollapsedWidth;

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
            => throw new NotSupportedException();
    }
}
