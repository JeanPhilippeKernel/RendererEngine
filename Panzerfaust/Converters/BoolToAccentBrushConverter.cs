using Avalonia.Data.Converters;
using Avalonia.Media;
using System;
using System.Globalization;

namespace Panzerfaust.Converters
{
    public class BoolToAccentBrushConverter : IValueConverter
    {
        public static readonly BoolToAccentBrushConverter Instance = new();

        private static readonly IBrush Active = new SolidColorBrush(Color.Parse("#0078D4"));
        private static readonly IBrush Inactive = new SolidColorBrush(Colors.Transparent);

        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
            => value is true ? Active : Inactive;

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
            => throw new NotSupportedException();
    }
}
