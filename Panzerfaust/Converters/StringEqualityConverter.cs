using Avalonia.Data.Converters;
using System;
using System.Globalization;

namespace Panzerfaust.Converters
{
    public class StringEqualityConverter : IValueConverter
    {
        public static readonly StringEqualityConverter Instance = new();

        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            bool match = value is string s && s == parameter as string;
            if (targetType == typeof(double)) return match ? 1.0 : 0.0;
            if (targetType == typeof(string)) return match ? "scaleY(1)" : "scaleY(0.3)";
            return match;
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
            => throw new NotSupportedException();
    }
}
