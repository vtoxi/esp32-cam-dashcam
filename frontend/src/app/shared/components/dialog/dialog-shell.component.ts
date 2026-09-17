import { Component, EventEmitter, HostListener, Input, Output } from '@angular/core';

/** Shared backdrop + centered card chrome for every dialog (confirm, create
 * webhook, issue command) — content-projected so each dialog only supplies
 * its own body/actions markup. */
@Component({
  selector: 'app-dialog-shell',
  standalone: true,
  template: `
    <div class="fixed inset-0 z-[90] flex items-center justify-center p-4">
      <div class="absolute inset-0 bg-black/60 backdrop-blur-sm" (click)="dismiss.emit()"></div>
      <div
        class="relative w-full rounded-2xl border border-white/10 bg-surface-300 shadow-2xl animate-in"
        [class]="maxWidthClass"
        role="dialog"
        aria-modal="true"
      >
        <div class="flex items-start justify-between gap-4 px-6 pt-5 pb-3">
          <h2 class="text-lg font-semibold text-white">{{ title }}</h2>
        </div>
        <div class="px-6 pb-6">
          <ng-content></ng-content>
        </div>
      </div>
    </div>
  `,
})
export class DialogShellComponent {
  @Input() title = '';
  @Input() maxWidthClass = 'max-w-md';
  @Output() dismiss = new EventEmitter<void>();

  @HostListener('document:keydown.escape')
  onEscape(): void {
    this.dismiss.emit();
  }
}
