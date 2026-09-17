import { Component, inject } from '@angular/core';
import { IconComponent } from '../icon/icon.component';
import { ToastService } from './toast.service';

const ICON_BY_KIND: Record<string, string> = {
  success: 'check',
  error: 'x-circle',
  info: 'information-circle',
};
const COLOR_BY_KIND: Record<string, string> = {
  success: 'border-emerald-400/30 text-emerald-300',
  error: 'border-rose-400/30 text-rose-300',
  info: 'border-sky-400/30 text-sky-300',
};

@Component({
  selector: 'app-toast-host',
  standalone: true,
  imports: [IconComponent],
  template: `
    <div class="fixed bottom-5 right-5 z-[100] flex flex-col gap-2 w-[min(380px,90vw)]">
      @for (toast of toasts.toasts(); track toast.id) {
        <div
          class="flex items-start gap-2.5 rounded-xl border bg-surface-300/95 backdrop-blur px-4 py-3 shadow-lg animate-in {{ colorClass(toast.kind) }}"
        >
          <app-icon [name]="iconFor(toast.kind)" class="w-5 h-5 mt-0.5 shrink-0" />
          <p class="text-sm text-slate-200 flex-1">{{ toast.message }}</p>
          <button
            type="button"
            class="focus-ring text-slate-500 hover:text-white shrink-0"
            (click)="toasts.dismiss(toast.id)"
            aria-label="Dismiss"
          >
            <app-icon name="x-mark" class="w-4 h-4" />
          </button>
        </div>
      }
    </div>
  `,
})
export class ToastHostComponent {
  readonly toasts = inject(ToastService);

  iconFor(kind: string): string {
    return ICON_BY_KIND[kind] ?? 'information-circle';
  }
  colorClass(kind: string): string {
    return COLOR_BY_KIND[kind] ?? '';
  }
}
