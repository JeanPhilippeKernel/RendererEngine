using ReactiveUI;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reactive;
using System.Text;
using System.Threading.Tasks;

namespace Panzerfaust.ViewModels
{
    internal class MessageBoxWindowViewModel : ViewModelBase
    {
        public ReactiveCommand<Unit, bool> CancelCommand { get; } = ReactiveCommand.CreateFromTask(() => Task.FromResult(false));
        public ReactiveCommand<Unit, bool> OkCommand { get; } = ReactiveCommand.CreateFromTask(() => Task.FromResult(true));
    }
}
