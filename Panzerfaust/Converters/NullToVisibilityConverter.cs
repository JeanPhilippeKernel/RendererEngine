using Avalonia.Data.Converters;
using System;
using System.Globalization;

namespace Panzerfaust.Converters
{
    public class NullToVisibilityConverter : IValueConverter
    {
        public static readonly NullToVisibilityConverter Instance = new();
        public static readonly NullToVisibilityConverter Inverse = new() { IsInverse = true };

        public bool IsInverse { get; set; }

        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            bool hasValue = value != null;
            return IsInverse ? !hasValue : hasValue;
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
            => throw new NotSupportedException();
    }
}
