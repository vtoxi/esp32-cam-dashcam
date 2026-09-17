import { ApplicationRef, EnvironmentInjector, Injectable, Type, inject } from '@angular/core';
import { createComponent } from '@angular/core';
import { ConfirmDialogComponent, ConfirmDialogData } from './confirm-dialog.component';

/** Handed to every dialog component (as a plain instance property, not an
 * @Input) so it can close itself and hand back a result. */
export interface DialogRef<R = unknown> {
  close(result?: R): void;
}

/**
 * Minimal, dependency-free modal host — Angular's own `createComponent` API
 * instead of Angular CDK's Overlay/Dialog module (no `@mat`/`@cdk` anywhere
 * in this app, per the "no Material components, Tailwind only" direction).
 * A dialog component just declares a public `dialogRef!: DialogRef<R>` field
 * and calls `this.dialogRef.close(result)`; DialogService wires it up,
 * mounts it detached from the router outlet, and resolves a Promise when
 * it's closed.
 */
@Injectable({ providedIn: 'root' })
export class DialogService {
  private readonly appRef = inject(ApplicationRef);
  private readonly injector = inject(EnvironmentInjector);

  open<C extends { dialogRef: DialogRef<R> }, R = unknown>(
    component: Type<C>,
    inputs: Partial<Omit<C, 'dialogRef'>> = {},
  ): Promise<R | undefined> {
    return new Promise((resolve) => {
      const host = document.createElement('div');
      document.body.appendChild(host);

      const compRef = createComponent(component, {
        environmentInjector: this.injector,
        hostElement: host,
      });

      Object.entries(inputs).forEach(([key, value]) => {
        compRef.setInput(key, value);
      });

      let settled = false;
      const close = (result?: R) => {
        if (settled) return;
        settled = true;
        resolve(result);
        this.appRef.detachView(compRef.hostView);
        compRef.destroy();
        host.remove();
      };

      compRef.instance.dialogRef = { close };
      this.appRef.attachView(compRef.hostView);
      compRef.changeDetectorRef.detectChanges();
    });
  }

  confirm(data: ConfirmDialogData): Promise<boolean> {
    return this.open<ConfirmDialogComponent, boolean>(ConfirmDialogComponent, { data }).then((r) => r ?? false);
  }
}
